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
static const float kLoudnessCompensationDb[18] = {
  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.7f, -1.6f, -2.5f,
  -3.4f, -4.0f, -4.5f, -4.9f, -5.2f, -5.4f, -5.8f, -6.0f,
  -6.0f,  // Guard: Interpolate() reads one past the end at 1.0.
};

// For short decays, the internal envelope is held at full level for a fraction
// of its decay time: this adds body (and loudness) without raising the peak.
const float kHoldRatio = 0.3f;
const float kMaxHoldTime = 0.03f;

// Overall trim, to make room for the dark short plucks: they are at the
// limiter's threshold and can't be boosted, so everything else comes down
// towards them.
const float kOutputTrimDb = -3.0f;

// The tables below were fitted to A-weighted level measurements of the drone
// (first 200ms, averaged over ratios 0.1 to 0.9 and 82/165/330Hz), see
// rings/tools/sim. Positive values only use the headroom left by the DAMPING
// compensation and the trim: the total gain never exceeds 0dB, so short
// plucks (already at the limiter's threshold) only get the cuts.

// vs. BRIGHTNESS (9 points from 0 to 1, plus a guard). Below noon the voice
// tends towards two pure sines, which sound ~5dB quieter than richer spectra
// at the same level.
static const float kBrightnessCompensationDb[10] = {
  5.2f, 5.0f, 3.7f, 1.5f, 0.0f, -0.8f, -1.3f, -1.2f, -1.0f, -1.0f,
};

// Classic feedback (right of noon) at full amount, vs. BRIGHTNESS. Scaled by
// the feedback amount.
static const float kFeedbackCompensationDb[10] = {
  -0.4f, -0.8f, -1.2f, -0.6f, 0.9f, 1.6f, 2.5f, 2.3f, 1.7f, 1.7f,
};

// Heavy DX7-style feedback with BRIGHTNESS between ~0.3 and 1 pushes energy
// into low sidebands: up to 4dB A-weighted drop at full feedback (averaged
// over ratios and pitches).
const float kSelfFeedbackMakeupDb = 4.0f;

// Dead band around the center of the feedback control (POSITION).
const float kFeedbackDeadBand = 0.03f;

// Maximum DX7-style self-feedback, in cycles of phase offset per unit of
// output. 0.5 is the DX7's level 7, which breaks into noise; 0.3 (about
// level 6) is the brightest setting that stays a clean saw.
const float kMaxSelfFeedback = 0.3f;

// Maps a feedback knob magnitude in [0, 1] to a self-feedback amount. The
// curve spreads the DX7's levels 1 to 7 (each a doubling) roughly evenly
// over the knob travel.
inline float SelfFeedbackAmount(float magnitude) {
  return kMaxSelfFeedback * (SemitonesToRatio(magnitude * 48.0f) - 1.0f) / 15.0f;
}

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
  previous_modulator_self_feedback_ = 0.0f;
  
  amplitude_envelope_ = 0.0f;
  brightness_envelope_ = 0.0f;
  
  carrier_phase_ = 0;
  modulator_phase_ = 0;
  previous_sample_ = 0.0f;
  modulator_history_[0] = modulator_history_[1] = 0.0f;
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
  float compensation_db = Interpolate(kLoudnessCompensationDb, damping_, 16.0f);
  compensation_db += Interpolate(kBrightnessCompensationDb, brightness_, 8.0f);
  if (feedback_amount_ > 0.5f) {
    float m = std::min((feedback_amount_ - 0.5f) * 2.0f, 1.0f);
    compensation_db += m * Interpolate(
        kFeedbackCompensationDb, brightness_, 8.0f);
  } else {
    float m = std::min((0.5f - feedback_amount_) * 2.0f, 1.0f);
    // Fitted to the measured drop: flat 0.5-0.6, fading out at 0.3 and 1.
    float w = std::min(
        std::max((brightness_ - 0.3f) * 5.0f, 0.0f),
        std::max((1.0f - brightness_) * 2.5f, 0.0f));
    compensation_db += kSelfFeedbackMakeupDb * m * m * m * std::min(w, 1.0f);
  }
  compensation_db += kOutputTrimDb;
  float loudness_compensation = SemitonesToRatio(
      std::min(compensation_db, 0.0f) * 1.993157f);
  
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

  // Left of noon: DX7-style modulator self-feedback. Right of noon: the
  // carrier modulates the modulator's phase (original Rings behaviour).
  float modulator_self_feedback = 0.0f;
  if (feedback < 0.0f) {
    modulator_self_feedback = SelfFeedbackAmount(feedback_magnitude);
    feedback = 0.0f;
  }
  // From here on, feedback is the classic (right side) amount, >= 0.
  
  ParameterInterpolator carrier_increment(
      &previous_carrier_frequency_, carrier_frequency_, size);
  ParameterInterpolator modulator_increment(
      &previous_modulator_frequency_, modulator_frequency, size);
  ParameterInterpolator brightness(
      &previous_brightness_, brightness_, size);
  ParameterInterpolator feedback_amount(
      &previous_feedback_amount_, feedback, size);
  ParameterInterpolator modulator_self_feedback_amount(
      &previous_modulator_self_feedback_, modulator_self_feedback, size);
  ParameterInterpolator compensation(
      &loudness_compensation_, loudness_compensation, size);

  uint32_t carrier_phase = carrier_phase_;
  uint32_t modulator_phase = modulator_phase_;
  float previous_sample = previous_sample_;
  float m_1 = modulator_history_[0];
  float m_2 = modulator_history_[1];
  
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
    modulator_phase += static_cast<uint32_t>(4294967296.0f * \
      modulator_increment.Next());
    carrier_phase += static_cast<uint32_t>(4294967296.0f * \
        carrier_increment.Next());

    float modulator_fb = 0.25f * feedback_amount.Next();
    // DX7-style self-feedback: average of the last two output samples.
    float modulator = SineFm(
        modulator_phase,
        modulator_fb * previous_sample + \
            modulator_self_feedback_amount.Next() * 0.5f * (m_1 + m_2));
    m_2 = m_1;
    m_1 = modulator;
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
  modulator_history_[0] = m_1;
  modulator_history_[1] = m_2;
}

}  // namespace rings
