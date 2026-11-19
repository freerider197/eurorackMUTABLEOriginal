// Tomline: control values to voice parameters. Pure code, no hardware
// access, so tomline/test can run it on a computer.

#include "tomline/parameters.h"

#include "streams/meta_parameters.h"

#include "tomline/tables.h"

namespace tomline {

using namespace streams;

// --- Float helpers for the control-rate maths (no libm needed). ---

// 2^x.
static float Exp2(float x) {
  int32_t integral = static_cast<int32_t>(x);
  if (x < 0.0f && static_cast<float>(integral) != x) {
    --integral;
  }
  float f = x - static_cast<float>(integral);
  float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f +
      f * (0.0096181f + f * 0.0013333f))));
  while (integral > 0) { p *= 2.0f; --integral; }
  while (integral < 0) { p *= 0.5f; ++integral; }
  return p;
}

// 1 - exp(-1 / (tau * sample_rate)), in Q31. Used for decay multipliers
// (Q31 value of exp(...) is 2^31 - result) and one-pole slew rates.
static int32_t OneMinusExpQ31(float tau) {
  float x = 1.0f / (tau * kSampleRate);
  if (x > 0.5f) x = 0.5f;
  float y = x * (1.0f - x * (0.5f - x * (0.16666667f - x * 0.04166667f)));
  return static_cast<int32_t>(y * 2147483648.0f);
}

static int32_t DecayQ31(float tau) {
  return 2147483647 - OneMinusExpQ31(tau);
}

static inline int32_t Q16(float x) { return static_cast<int32_t>(x * 65536.0f); }
static inline int32_t Q15(float x) { return static_cast<int32_t>(x * 32767.0f); }

// 0..1 (clamped) to a crossfade amount, 32768 = full.
static inline int32_t Mix(float x) {
  if (x < 0.0f) x = 0.0f;
  if (x > 1.0f) x = 1.0f;
  return static_cast<int32_t>(x * 32768.0f);
}

void ComputeVoiceParameters(
    uint8_t engine,
    uint8_t snap,
    const uint16_t* main_layer,
    const uint16_t* second_layer,
    const uint16_t* lfo_layer,
    uint8_t lfo_shape,
    VoiceParameters* p,
    bool lfo_free) {
  bool bt = engine == ENGINE_BT;
  float decay = main_layer[KNOB_SHAPE_1] / 65535.0f;
  float tune = main_layer[KNOB_SHAPE_2] / 65535.0f;
  float distortion = main_layer[KNOB_MOD_2] / 65535.0f;
  float crush = second_layer[KNOB_SHAPE_1] / 65535.0f;
  float octave = second_layer[KNOB_MOD_1] / 65535.0f;
  float wave = second_layer[KNOB_SHAPE_2] / 65535.0f;
  float glide = second_layer[KNOB_MOD_2] / 65535.0f;
  float lfo_speed = lfo_layer[KNOB_SHAPE_1] / 65535.0f;
  float lfo_depth = lfo_layer[KNOB_MOD_1] / 65535.0f;

  // Decay time constant: 15 ms .. 2.5 s.
  float tau = 0.015f * Exp2(decay * 7.38082f);  // 166.67 = 2^7.38082
  const float snap_tau[4] = { 0.0004f, 0.0015f, 0.004f, 0.010f };
  float harmonic = bt ? 0.1f : 0.3f;

  VoiceParameters& n = *p;
  n.engine = engine;
  n.amp_coefficient = DecayQ31(tau);
  n.partial_coefficient = DecayQ31(tau * 0.3f);
  n.snap_coefficient = DecayQ31(snap_tau[snap]);
  n.glide_k = glide < 0.01f
      ? 0
      : OneMinusExpQ31(0.004f + glide * glide * 0.5f);
  // +/-12 semitones; at noon, 0 V on EXCITE 2 plays C2.
  n.tune = Q16(12.0f + (tune - 0.5f) * 24.0f);
  n.snap_depth = Q16(bt ? 30.0f : 15.0f);
  n.octave_level = Q15(octave);
  n.harmonic_level = Q15(harmonic);
  n.normalization = Q15(1.0f / (1.0f + 0.6f * harmonic + 0.55f * octave));
  n.snap_noise = Q15(bt ? 0.5f : 0.3f);
  n.drive = bt ? 4915 : 4300;  // Q12: 1.2 / 1.05
  // Soft distortion: gain 1 to 4 (+12 dB) on a squared curve, divided by
  // the soft clipper's slope of 1.5, so quiet sounds keep their level.
  // Fully CCW = off, the clean sound.
  n.distortion = distortion < 0.01f
      ? 0
      : static_cast<int32_t>((1.0f + 3.0f * distortion * distortion) *
                             (4096.0f / 1.5f));
  // Wave: a third of the knob each for sine -> triangle, triangle -> folded
  // sine (fold gain 1 to 2: the top folds back down, the fundamental stays
  // within about 4 dB), folded -> square. Fully CCW = the plain sine.
  float w3 = wave < 0.01f ? 0.0f : wave * 3.0f;
  float fold = w3 - 1.0f;
  if (fold < 0.0f) fold = 0.0f;
  if (fold > 1.0f) fold = 1.0f;
  n.wave_tri = Mix(w3);
  n.wave_fold = Mix(fold);
  n.fold_gain = static_cast<int32_t>((1.0f + fold) * 4096.0f);
  n.wave_square = Mix(w3 - 2.0f);
  // Level along the Wave knob (17 points, dB): the triangle and the end of
  // the fold are quieter than the sine at the same peak, the square louder.
  // Measured with render.cc's level check, BT and LT averaged.
  static const float kWaveLevelDb[17] = {
    0.00f, -0.25f, -0.55f, -0.80f, -1.15f, -1.45f, -1.15f, -0.55f, -0.45f,
    -0.85f, -1.90f, -2.60f, -1.50f, -0.45f, 0.45f, 1.25f, 2.00f
  };
  float pos = wave * 16.0f;
  int32_t i = static_cast<int32_t>(pos);
  if (i > 15) i = 15;
  float level = kWaveLevelDb[i] + (kWaveLevelDb[i + 1] - kWaveLevelDb[i]) * (pos - i);
  n.wave_gain = static_cast<int32_t>(Exp2(-level * 0.1660964f) * 4096.0f);
  // Crush. First half: the sample rate falls from 31 kHz to 300 Hz
  // (2^-6.695); a bass note needs a rate this low for strong aliasing.
  // Second half, at 300 Hz: the level step grows from 1 to 4096 (8 levels
  // each side of zero). Fully CCW = off.
  if (crush < 0.01f) {
    n.crush_increment = 0;
    n.crush_step = 1;
  } else {
    float rate = crush * 2.0f;
    if (rate > 1.0f) rate = 1.0f;
    float levels = crush * 2.0f - 1.0f;
    if (levels < 0.0f) levels = 0.0f;
    // 2147483520 = the largest float below 2^31.
    n.crush_increment = static_cast<int32_t>(
        Exp2(rate * -6.695f) * 2147483520.0f);
    n.crush_step = static_cast<int32_t>(Exp2(levels * 12.0f));
  }
  ComputeAmountOffset(main_layer[KNOB_MOD_1], &n.tone_amount, &n.tone_offset);

  // Pitch LFO: 0.05 Hz .. 20 Hz (1 Hz at noon), depth up to +/-12 semitones
  // on a squared curve, so small vibrato depths are easy to set.
  float lfo_hz = 0.05f * Exp2(lfo_speed * 8.643856f);  // 400 = 2^8.643856
  n.lfo_increment = static_cast<int32_t>(lfo_hz * (4294967296.0f / kSampleRate));
  n.lfo_depth = lfo_depth < 0.01f ? 0 : Q16(lfo_depth * lfo_depth * 12.0f);
  n.lfo_shape = lfo_shape == LFO_RAMP_UP ? LFO_RAMP_UP : LFO_TRIANGLE;
  n.lfo_free = lfo_free ? 1 : 0;

}

}  // namespace tomline
