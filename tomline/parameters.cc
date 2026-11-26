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

// Wave (SHAPE 2, plus the LEVEL 2 CV): only the wave fields of p. ui.cc
// calls it alone when the CV moves, without the rest of the maths.
void ComputeWaveParameters(uint16_t wave_knob, VoiceParameters* p) {
  float wave = wave_knob / 65535.0f;
  // Wave: a third of the knob each for sine -> triangle, triangle -> folded
  // sine (fold gain 1 to 2: the top folds back down, the fundamental stays
  // within about 4 dB), folded -> square. Fully CCW = the plain sine.
  float w3 = wave < 0.01f ? 0.0f : wave * 3.0f;
  float fold = w3 - 1.0f;
  if (fold < 0.0f) fold = 0.0f;
  if (fold > 1.0f) fold = 1.0f;
  p->wave_tri = Mix(w3);
  p->wave_fold = Mix(fold);
  p->fold_gain = static_cast<int32_t>((1.0f + fold) * 4096.0f);
  p->wave_square = Mix(w3 - 2.0f);
  // Level along the Wave knob (17 points, dB): the triangle and the end of
  // the fold are quieter than the sine at the same peak, the square louder.
  // Measured with render.cc's level check, BT and LT averaged.
  static const float kWaveLevelDb[17] = {
    0.00f, -0.30f, -0.60f, -1.00f, -1.30f, -1.70f, -1.25f, -0.60f, -0.30f,
    -0.55f, -1.70f, -2.65f, -1.30f, -0.10f, 0.80f, 1.60f, 2.25f
  };
  float pos = wave * 16.0f;
  int32_t i = static_cast<int32_t>(pos);
  if (i > 15) i = 15;
  float level = kWaveLevelDb[i] + (kWaveLevelDb[i + 1] - kWaveLevelDb[i]) * (pos - i);
  p->wave_gain = static_cast<int32_t>(Exp2(-level * 0.1660964f) * 4096.0f);
}

void ComputeVoiceParameters(
    uint8_t engine,
    uint8_t snap,
    const uint16_t* main_layer,
    const uint16_t* second_layer,
    const uint16_t* lfo_layer,
    uint8_t lfo_shape,
    VoiceParameters* p,
    bool lfo_free,
    const uint16_t* duck_layer) {
  bool bt = engine == ENGINE_BT;
  float decay = main_layer[KNOB_SHAPE_1] / 65535.0f;
  float tune = main_layer[KNOB_SHAPE_2] / 65535.0f;
  float distortion = main_layer[KNOB_MOD_2] / 65535.0f;
  float crush = second_layer[KNOB_SHAPE_1] / 65535.0f;
  // second_layer[KNOB_MOD_1] is unused (it was the +1 octave).
  float glide = second_layer[KNOB_MOD_2] / 65535.0f;
  float lfo_speed = lfo_layer[KNOB_SHAPE_1] / 65535.0f;
  float lfo_depth = lfo_layer[KNOB_MOD_1] / 65535.0f;

  // Decay time constant: 95 ms .. 2.5 s (shorter was too short to use).
  float tau = 0.095f * Exp2(decay * 4.717857f);  // 26.316 = 2^4.717857
  // Snap: off, then a click (noise and a pitch blip) of 0.4, 1.5 or 4 ms.
  const float snap_tau[4] = { 0.0004f, 0.0004f, 0.0015f, 0.004f };
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
  n.snap_depth = snap ? Q16(bt ? 30.0f : 15.0f) : 0;
  n.harmonic_level = Q15(harmonic);
  n.normalization = Q15(1.0f / (1.0f + 0.6f * harmonic));
  n.snap_noise = snap ? Q15(bt ? 0.5f : 0.3f) : 0;
  n.drive = bt ? 4915 : 4300;  // Q12: 1.2 / 1.05
  // Soft distortion: gain 1 to 4 (+12 dB) on a squared curve, divided by
  // the soft clipper's slope of 1.5, so quiet sounds keep their level.
  // Fully CCW = off, the clean sound.
  n.distortion = distortion < 0.01f
      ? 0
      : static_cast<int32_t>((1.0f + 3.0f * distortion * distortion) *
                             (4096.0f / 1.5f));
  // Without a correction the hit gets louder as MOD 2 goes up, by up to
  // 6 dB with a short decay and 2 dB with a long one. Level rise in dB
  // (RMS of the first 0.3 s of a C2 hit, BT and LT averaged, measured with
  // render.cc's level check), for Decay 0 / 25 / 50 / 75 / 100 % (rows) and
  // MOD 2 every 1/8 (columns). The gain after the clipper takes it back off.
  static const float kDistortionRiseDb[5][9] = {
    { 0.00f, -0.19f, 0.73f, 1.95f, 3.17f, 4.22f, 5.09f, 5.80f, 6.39f },
    { 0.00f, -0.23f, 0.67f, 1.87f, 3.04f, 4.00f, 4.75f, 5.31f, 5.71f },
    { 0.00f, -0.38f, 0.48f, 1.57f, 2.54f, 3.19f, 3.55f, 3.76f, 3.89f },
    { 0.00f, -0.57f, 0.22f, 1.18f, 1.90f, 2.26f, 2.47f, 2.62f, 2.73f },
    { 0.00f, -0.71f, 0.04f, 0.90f, 1.45f, 1.74f, 1.93f, 2.06f, 2.16f },
  };
  float dr = decay * 4.0f;
  int32_t r = static_cast<int32_t>(dr);
  if (r > 3) r = 3;
  float dc = distortion * 8.0f;
  int32_t c = static_cast<int32_t>(dc);
  if (c > 7) c = 7;
  float fr = dr - r, fc = dc - c;
  float top = kDistortionRiseDb[r][c] +
      (kDistortionRiseDb[r][c + 1] - kDistortionRiseDb[r][c]) * fc;
  float bottom = kDistortionRiseDb[r + 1][c] +
      (kDistortionRiseDb[r + 1][c + 1] - kDistortionRiseDb[r + 1][c]) * fc;
  float rise = top + (bottom - top) * fr;
  n.distortion_gain = static_cast<int32_t>(Exp2(-rise * 0.1660964f) * 4096.0f);
  ComputeWaveParameters(second_layer[KNOB_SHAPE_2], &n);
  // Crush: the sample rate goes from 6 kHz, just past fully CCW (off), down
  // to 750 Hz at fully CW, 3 octaves (the owner's useful range). 2147483520
  // = the largest float below 2^31.
  n.crush_increment = crush < 0.01f
      ? 0
      : static_cast<int32_t>(Exp2(crush * -3.0f) *
                             (6000.0f / kSampleRate * 2147483520.0f));
  ComputeAmountOffset(main_layer[KNOB_MOD_1], &n.tone_amount, &n.tone_offset);

  // Pitch LFO: 0.05 Hz .. 20 Hz (1 Hz at noon), depth up to +/-12 semitones
  // on a squared curve, so small vibrato depths are easy to set.
  float lfo_hz = 0.05f * Exp2(lfo_speed * 8.643856f);  // 400 = 2^8.643856
  n.lfo_increment = static_cast<int32_t>(lfo_hz * (4294967296.0f / kSampleRate));
  n.lfo_depth = lfo_depth < 0.01f ? 0 : Q16(lfo_depth * lfo_depth * 12.0f);
  n.lfo_shape = lfo_shape == LFO_RAMP_UP ? LFO_RAMP_UP : LFO_TRIANGLE;
  n.lfo_free = lfo_free ? 1 : 0;

  // Pitch sweep (METER + SHAPE 2): up to +12 semitones on a squared curve, so small
  // amounts are easy to set, falling back with a 20 ms time constant (within
  // 10 cents of the note 100 ms after the hit at full amount).
  float sweep = lfo_layer[KNOB_MOD_2] / 65535.0f;
  n.sweep_depth = sweep < 0.01f ? 0 : Q16(sweep * sweep * 12.0f);
  n.sweep_coefficient = DecayQ31(0.02f);

  // Sidechain duck (FN 1 + SHAPE 2 / MOD 2): depth on a squared curve, so
  // a light duck is easy to set; fully CW silences the bass at the kick's
  // peak. Release: the bass is fully back 40 ms after the hit at fully CCW,
  // 179 ms at noon, 800 ms at fully CW (sooner if the kick is shorter).
  float duck_depth = duck_layer ? duck_layer[0] / 65535.0f : 0.0f;
  float duck_release = duck_layer ? duck_layer[1] / 65535.0f : 0.0f;
  n.duck_depth = duck_depth < 0.01f ? 0 : Q15(duck_depth * duck_depth);
  float release = 0.04f * Exp2(duck_release * 4.3219281f);  // 20 = 2^4.322
  n.duck_release = static_cast<int32_t>(2147483520.0f / (release * kSampleRate));
}

}  // namespace tomline
