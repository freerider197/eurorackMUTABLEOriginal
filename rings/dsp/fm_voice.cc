// Copyright 2015 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// FM Voice.

#include "rings/dsp/fm_voice.h"

#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"

#include "rings/resources.h"

namespace rings {

using namespace stmlib;

// Loudness compensation, in dB, as a function of DAMPING (17 points from 0 to
// 1). Short decays carry much less energy over the ear's ~200ms integration
// window than long decays or the drone, so without compensation the perceived
// level changes by ~16dB across the knob. The peak level is already at the
// limiter's threshold, so short decays get louder through the hold stage below,
// and long decays/drone are pulled down. Residual spread is ~3dB.
static const float kLoudnessCompensationDb[17] = {
  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.7f, -1.6f, -2.5f,
  -3.4f, -4.0f, -4.5f, -4.9f, -5.2f, -5.4f, -5.8f, -6.0f,
};

// For short decays, the internal envelope is held at full level for a fraction
// of its decay time: this adds body (and loudness) without raising the peak.
const float kHoldRatio = 0.3f;
const float kMaxHoldTime = 0.03f;

// Dead band around the center of the feedback control (POSITION).
const float kFeedbackDeadBand = 0.03f;

void FMVoice::Init() {
  set_frequency(220.0f / kSampleRate);
  set_ratio(0.5f);
  set_brightness(0.5f);
  set_damping(0.5f);
  set_position(0.5f);
  set_feedback_amount(0.0f);
  
  previous_carrier_frequency_ = carrier_frequency_;
  previous_modulator_frequency_ = carrier_frequency_;
  previous_brightness_ = brightness_;
  previous_damping_ = damping_;
  previous_feedback_amount_ = feedback_amount_;
  
  amplitude_envelope_ = 0.0f;
  brightness_envelope_ = 0.0f;
  
  carrier_phase_ = 0;
  modulator_phase_ = 0;
  gain_ = 0.0f;
  fm_amount_ = 0.0f;
  loudness_compensation_ = 1.0f;
  hold_samples_ = 0;
  hold_counter_ = 0;
  
  follower_.Init(
      8.0f / kSampleRate,
      160.0f / kSampleRate,
      1600.0f / kSampleRate);
}

void FMVoice::Process(const float* in, float* out, float* aux, size_t size) {
  // Interpolate between the "oscillator" behaviour and the "FMLPGed thing"
  // behaviour.
  float envelope_amount = damping_ < 0.9f ? 1.0f : (1.0f - damping_) * 10.0f;
  float amplitude_rt60 = 0.1f * SemitonesToRatio(damping_ * 96.0f) * kSampleRate;
  float amplitude_decay = 1.0f - powf(0.001f, 1.0f / amplitude_rt60);

  float brightness_rt60 = 0.1f * SemitonesToRatio(damping_ * 84.0f) * kSampleRate;
  float brightness_decay = 1.0f - powf(0.001f, 1.0f / brightness_rt60);

  hold_samples_ = static_cast<int32_t>(
      std::min(kHoldRatio * amplitude_rt60, kMaxHoldTime * kSampleRate));
  // dB to semitones: 12 * log2(10) / 20.
  float loudness_compensation = SemitonesToRatio(
      Interpolate(kLoudnessCompensationDb, damping_, 16.0f) * 1.993157f);
  
  float ratio = Interpolate(lut_fm_frequency_quantizer, ratio_, 128.0f);
  float modulator_frequency = carrier_frequency_ * SemitonesToRatio(ratio);
  
  if (modulator_frequency > 0.5f) {
    modulator_frequency = 0.5f;
  }
  
  float feedback = (feedback_amount_ - 0.5f) * 2.0f;
  // Linear response with a small dead band around the center. The amount used
  // to be squared, which left a wide region around noon with no audible
  // effect.
  float feedback_magnitude = std::max(
      fabsf(feedback) - kFeedbackDeadBand, 0.0f) / (1.0f - kFeedbackDeadBand);
  feedback = feedback < 0.0f ? -feedback_magnitude : feedback_magnitude;
  
  ParameterInterpolator carrier_increment(
      &previous_carrier_frequency_, carrier_frequency_, size);
  ParameterInterpolator modulator_increment(
      &previous_modulator_frequency_, modulator_frequency, size);
  ParameterInterpolator brightness(
      &previous_brightness_, brightness_, size);
  ParameterInterpolator feedback_amount(
      &previous_feedback_amount_, feedback, size);
  ParameterInterpolator compensation(
      &loudness_compensation_, loudness_compensation, size);

  uint32_t carrier_phase = carrier_phase_;
  uint32_t modulator_phase = modulator_phase_;
  float previous_sample = previous_sample_;
  
  while (size--) {
    // Envelope follower and internal envelope.
    float amplitude_envelope, brightness_envelope;
    follower_.Process(
        *in++,
        &amplitude_envelope,
        &brightness_envelope);
    
    brightness_envelope *= 2.0f * amplitude_envelope * (2.0f - amplitude_envelope);
    
    if (hold_counter_) {
      --hold_counter_;
      SLOPE(amplitude_envelope_, amplitude_envelope, 0.05f, 0.0f);
    } else {
      SLOPE(amplitude_envelope_, amplitude_envelope, 0.05f, amplitude_decay);
    }
    SLOPE(brightness_envelope_, brightness_envelope, 0.01f, brightness_decay);
    
    // Compute envelopes.
    float brightness_value = brightness.Next();
    brightness_value *= brightness_value;
    float fm_amount_min = brightness_value < 0.5f
        ? 0.0f
        : brightness_value * 2.0f - 1.0f;
    float fm_amount_max = brightness_value < 0.5f
        ? 2.0f * brightness_value
        : 1.0f;
    float fm_envelope = 0.5f + envelope_amount * (brightness_envelope_ - 0.5f);
    float fm_amount = (fm_amount_min + fm_amount_max * fm_envelope) * 2.0f;
    SLEW(fm_amount_, fm_amount, 0.005f + fm_amount_max * 0.015f);

    // FM synthesis in itself
    float feedback = feedback_amount.Next();
    float phase_feedback = feedback < 0.0f ? -0.5f * feedback : 0.0f;
    modulator_phase += static_cast<uint32_t>(4294967296.0f * \
      modulator_increment.Next() * (1.0f + previous_sample * phase_feedback));
    carrier_phase += static_cast<uint32_t>(4294967296.0f * \
        carrier_increment.Next());

    float modulator_fb = feedback > 0.0f ? 0.25f * feedback : 0.0f;
    float modulator = SineFm(modulator_phase, modulator_fb * previous_sample);
    float carrier = SineFm(carrier_phase, fm_amount_ * modulator);
    ONE_POLE(previous_sample, carrier, 0.1f);

    // Compute amplitude envelope.
    float gain = 1.0f + envelope_amount * (amplitude_envelope_ - 1.0f);
    gain *= compensation.Next();
    ONE_POLE(gain_, gain, 0.005f + 0.045f * fm_amount_);
    
    *out++ = (carrier + 0.5f * modulator) * gain_;
    *aux++ = 0.5f * modulator * gain_;
  }
  carrier_phase_ = carrier_phase;
  modulator_phase_ = modulator_phase;
  previous_sample_ = previous_sample;
}

}  // namespace rings
