// Tomline: bass tom / low tom voice for the Mutable Instruments Streams
// hardware. See voice.h.

#include "tomline/voice.h"

#include "tomline/tables.h"

namespace tomline {

const int32_t kUnity = 1 << 30;           // Q30
// 0.4 ms linear attack at the firmware rate: 2^30 / (0.0004 * 31088).
const int32_t kAttackStep = 86343000;
// Highest phase increment allowed: 0.45 of the firmware sample rate.
const uint32_t kMaxIncrement = 1932735283UL;
// 1.593: inharmonic partial of the low tom, Q16.
const uint32_t kPartialRatio = 104399;
// On retrigger the old note fades out over 48 samples (1.5 ms).
const int32_t kFadeStep = 683;

static inline int32_t Sine(uint32_t phase) {
  uint32_t index = phase >> 22;
  int32_t a = lut_sine[index];
  int32_t b = lut_sine[index + 1];
  int32_t fractional = (phase >> 6) & 0xffff;
  return a + ((b - a) * fractional >> 16);
}

// Semitones above C1 (Q16) to a phase increment at the firmware rate.
static inline uint32_t SemitonesToIncrement(int32_t semitones) {
  // Divide by 12 to get octaves, Q16. 5592405 / 2^26 = 1 / 12.
  int32_t octaves = static_cast<int32_t>(
      static_cast<int64_t>(semitones) * 5592405 >> 26);
  int32_t integral = octaves >> 16;  // floor, also for negative values
  uint32_t fractional = octaves & 0xffff;
  uint32_t index = fractional >> 8;
  uint32_t a = lut_exp2_q30[index];
  uint32_t b = lut_exp2_q30[index + 1];
  uint32_t mantissa = a + ((b - a) * (fractional & 0xff) >> 8);
  uint32_t increment = static_cast<uint32_t>(
      static_cast<uint64_t>(kIncrementC1) * mantissa >> 30);
  if (integral >= 0) {
    if (integral > 8) {
      return kMaxIncrement;
    }
    uint64_t shifted = static_cast<uint64_t>(increment) << integral;
    return shifted > kMaxIncrement ? kMaxIncrement : shifted;
  } else {
    return integral < -24 ? 0 : increment >> (-integral);
  }
}

static inline int32_t Decay(int32_t value, int32_t coefficient) {
  return static_cast<int32_t>(
      static_cast<int64_t>(value) * coefficient >> 31);
}

void Voice::Init() {
  semitones_ = 0;
  gliding_ = false;
  phase_ = 0;
  partial_phase_ = 0;
  lfo_phase_ = 0;
  amp_ = 0;
  attack_ = kUnity;
  sweep_ = 0;
  snap_ = 0;
  noise_ = 0;
  partial_ = 0;
  bias_ = 0;
  last_mix_ = 0;
  tail_ = 0;
  fade_ = 0;
  rng_state_ = 0x21f3a5c1;
  noise_lp1_ = 0;
  noise_lp2_ = 0;
}

void Voice::Process(
    const VoiceParameters& p,
    bool trigger,
    int32_t pitch,
    uint16_t* gain,
    uint16_t* frequency) {
  if (trigger) {
    // Glide only when the previous note is still sounding (above -36 dB).
    gliding_ = p.glide_k != 0 && amp_ > (kUnity >> 6);
    amp_ = kUnity;
    attack_ = 0;
    sweep_ = kUnity;
    snap_ = kUnity;
    noise_ = kUnity;
    partial_ = kUnity;
    phase_ = 0;
    partial_phase_ = 0;
    // The new note starts at once; the old one fades out instead of
    // dropping to zero in one sample (a click).
    tail_ = last_mix_;
    fade_ = 32767;
  }

  // Pitch.
  int32_t target = pitch + p.tune;
  if (gliding_) {
    int32_t error = target - semitones_;
    int32_t step = static_cast<int32_t>(
        static_cast<int64_t>(error) * p.glide_k >> 31);
    // Integer rounding would stop the glide a few cents short: always move
    // by at least one step (1/65536 semitone) until the target is reached.
    if (step == 0 || step == -1) {
      step = error > 0 ? 1 : (error < 0 ? -1 : 0);
    }
    semitones_ += step;
  } else {
    semitones_ = target;
  }

  // Envelopes.
  amp_ = Decay(amp_, p.amp_coefficient);
  sweep_ = Decay(sweep_, p.sweep_coefficient);
  snap_ = Decay(snap_, p.snap_coefficient);
  noise_ = Decay(noise_, p.noise_coefficient);
  partial_ = Decay(partial_, p.partial_coefficient);
  if (attack_ < kUnity) {
    attack_ += kAttackStep;
    if (attack_ > kUnity) {
      attack_ = kUnity;
    }
  }
  int32_t env = static_cast<int32_t>(
      static_cast<int64_t>(amp_) * attack_ >> 45);  // Q15

  // Pitch LFO, added after the glide so the glide does not smooth it.
  lfo_phase_ += static_cast<uint32_t>(p.lfo_increment);
  int32_t s = semitones_;
  if (p.lfo_depth) {
    s += static_cast<int32_t>(static_cast<int64_t>(p.lfo_depth) *
                              LfoWave(lfo_phase_, p.lfo_shape) >> 15);
  }

  // Oscillators.
  s += static_cast<int32_t>(static_cast<int64_t>(p.sweep_depth) * sweep_ >> 30);
  s += static_cast<int32_t>(static_cast<int64_t>(p.snap_depth) * snap_ >> 30);
  uint32_t increment = SemitonesToIncrement(s);
  phase_ += increment;

  int32_t fundamental = Sine(phase_);
  int32_t octave = Sine(phase_ << 1);
  int32_t x = fundamental;
  if (p.engine == ENGINE_BT) {
    x += octave * p.harmonic_level >> 15;
  } else {
    partial_phase_ += static_cast<uint32_t>(
        static_cast<uint64_t>(increment) * kPartialRatio >> 16);
    int32_t level = static_cast<int32_t>(
        static_cast<int64_t>(p.harmonic_level) * partial_ >> 30);
    x += Sine(partial_phase_) * level >> 15;
  }
  x += octave * p.octave_level >> 15;
  x = static_cast<int32_t>(static_cast<int64_t>(x) * p.normalization >> 15);

  // Drive into a cubic soft clipper.
  x = x * p.drive >> 12;
  CONSTRAIN(x, -32767, 32767);
  int32_t cube = ((x * x) >> 15) * x >> 15;
  x = (3 * x - cube) >> 1;
  CONSTRAIN(x, -32767, 32767);

  // Noise: band-passed white noise with its own short decay, plus the snap
  // click.
  rng_state_ = rng_state_ * 1664525UL + 1013904223UL;
  int32_t white = static_cast<int16_t>(rng_state_ >> 16);
  noise_lp1_ += (white - noise_lp1_) * p.noise_k1 >> 15;
  noise_lp2_ += (noise_lp1_ - noise_lp2_) * p.noise_k2 >> 15;
  int32_t band = (noise_lp1_ - noise_lp2_) << 1;
  CLIP(band);
  // Noise and snap share the tone's 0.4 ms attack, so a retrigger through an
  // already open Tone filter does not click.
  int32_t noise_env = static_cast<int32_t>(
      static_cast<int64_t>(noise_) * attack_ >> 45);
  int32_t snap_env = static_cast<int32_t>(
      static_cast<int64_t>(snap_) * attack_ >> 45);

  int32_t v = x * env >> 15;
  v += (p.noise_level * noise_env >> 15) * band >> 15;
  v += (p.snap_noise * snap_env >> 15) * white >> 15;
  v = ScaleMix(v);
  if (fade_ > 0) {
    fade_ -= kFadeStep;
    if (fade_ < 0) {
      fade_ = 0;
    }
    v += tail_ * fade_ >> 15;
    CLIP(v);
  }
  last_mix_ = v;

  // Lift the wave by its own peak level so the VCA word never goes below
  // zero. The lift decays with the amplitude envelope, so OUT 1 goes back to
  // 0 V when the note dies out.
  int32_t magnitude = v < 0 ? -v : v;
  int32_t decayed = Decay(bias_, p.amp_coefficient);
  bias_ = magnitude > decayed ? magnitude : decayed;
  int32_t g = bias_ + v;
  CONSTRAIN(g, 0, 65535);
  *gain = g;

  // Filter: same amount/offset scheme as the stock envelope mode.
  int32_t f = p.tone_offset + (env * p.tone_amount >> 15);
  CONSTRAIN(f, 0, 65535);
  *frequency = f;
}

}  // namespace tomline
