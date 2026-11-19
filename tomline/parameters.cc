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

void ComputeVoiceParameters(
    uint8_t engine,
    uint8_t snap,
    const uint16_t* main_layer,
    const uint16_t* second_layer,
    VoiceParameters* p) {
  bool bt = engine == ENGINE_BT;
  float decay = main_layer[KNOB_SHAPE_1] / 65535.0f;
  float tune = main_layer[KNOB_SHAPE_2] / 65535.0f;
  float sweep = main_layer[KNOB_MOD_2] / 65535.0f;
  float noise = second_layer[KNOB_SHAPE_1] / 65535.0f;
  float octave = second_layer[KNOB_MOD_1] / 65535.0f;
  float sweep_time = second_layer[KNOB_SHAPE_2] / 65535.0f;
  float glide = second_layer[KNOB_MOD_2] / 65535.0f;

  // Decay time constant: 15 ms .. 2.5 s.
  float tau = 0.015f * Exp2(decay * 7.38082f);  // 166.67 = 2^7.38082
  float noise_tau = tau * 0.35f;
  if (noise_tau > 0.25f) noise_tau = 0.25f;
  float sweep_tau = (0.006f + sweep_time * sweep_time * 0.3f) * (bt ? 0.6f : 1.0f);
  const float snap_tau[4] = { 0.0004f, 0.0015f, 0.004f, 0.010f };
  float harmonic = bt ? 0.1f : 0.3f;

  VoiceParameters& n = *p;
  n.engine = engine;
  n.amp_coefficient = DecayQ31(tau);
  n.partial_coefficient = DecayQ31(tau * 0.3f);
  n.noise_coefficient = DecayQ31(noise_tau);
  n.sweep_coefficient = DecayQ31(sweep_tau);
  n.snap_coefficient = DecayQ31(snap_tau[snap]);
  n.glide_k = glide < 0.01f
      ? 0
      : OneMinusExpQ31(0.004f + glide * glide * 0.5f);
  // +/-12 semitones; at noon, 0 V on EXCITE 2 plays C2.
  n.tune = Q16(12.0f + (tune - 0.5f) * 24.0f);
  n.sweep_depth = Q16(sweep * (bt ? 12.0f : 24.0f));
  n.snap_depth = Q16(bt ? 30.0f : 15.0f);
  n.octave_level = Q15(octave);
  n.harmonic_level = Q15(harmonic);
  n.normalization = Q15(1.0f / (1.0f + 0.6f * harmonic + 0.55f * octave));
  n.noise_level = Q15(noise * (bt ? 0.6f : 1.0f));
  n.snap_noise = Q15(bt ? 0.5f : 0.3f);
  // 1 - exp(-2 pi f / 31088) for the noise band: BT 300-1200 Hz,
  // LT 625-2500 Hz.
  n.noise_k1 = bt ? 7058 : 12996;
  n.noise_k2 = bt ? 1928 : 3889;
  n.drive = bt ? 4915 : 4300;  // Q12: 1.2 / 1.05
  ComputeAmountOffset(main_layer[KNOB_MOD_1], &n.tone_amount, &n.tone_offset);

}

}  // namespace tomline
