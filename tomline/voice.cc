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
// Lowest filter word at C1: a cutoff of 1.25 x 32.7 Hz, a third above.
const int32_t kToneFloor = 25277;
// On retrigger the old note fades out over 48 samples (1.5 ms).
const int32_t kFadeStep = 683;
// Mid scale of the VCA word: OUT 1 rests at about 3.33 V.
const int32_t kLift = 32767;
// Power-up: 1 s with the VCA word at zero, so Ui::CheckSetup can read LEVEL
// MOD 1 (it needs > 4096 samples at zero in one 0.5 s window). The lift then
// rises by one step per sample (about 1 s), slow enough that an AC-coupled
// input downstream does not thump.
const int32_t kPowerUpHold = 31088;

static inline int32_t Sine(uint32_t phase) {
  uint32_t index = phase >> 22;
  int32_t a = lut_sine[index];
  int32_t b = lut_sine[index + 1];
  int32_t fractional = (phase >> 6) & 0xffff;
  return a + ((b - a) * fractional >> 16);
}

// Wave (METER + SHAPE 2) on the main tone: sine -> triangle -> folded sine
// -> square. Each stage crossfades from the one before.
static inline int32_t Wave(
    const VoiceParameters& p, uint32_t phase, int32_t sine) {
  // Triangle in phase with the sine: 0 at phase 0, top at a quarter.
  uint32_t shifted = phase + 0x40000000U;  // wraps, also on 64-bit hosts
  int32_t u = static_cast<int32_t>(shifted >> 16);
  int32_t tri = 32768 - (u < 32768 ? 32768 - u : u - 32768) * 2;
  CONSTRAIN(tri, -32767, 32767);
  int32_t y = sine + ((tri - sine) * p.wave_tri >> 15);
  if (p.wave_fold) {
    // sin(pi / 2 * gain * sine): at gain 1 a rounder sine, turning up
    // folds the top of the wave back down.
    int32_t v = sine * p.fold_gain >> 12;
    int32_t folded = Sine(static_cast<uint32_t>(v) << 15);
    y += (folded - y) * p.wave_fold >> 15;
  }
  if (p.wave_square) {
    // The sine driven 4x into the soft clipper: a square with soft edges.
    int32_t square = Voice::SoftClip(sine << 2);
    y += (square - y) * p.wave_square >> 15;
  }
  return y;
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
  snap_ = 0;
  partial_ = 0;
  lift_ = 0;
  hold_ = kPowerUpHold;
  last_mix_ = 0;
  tail_ = 0;
  fade_ = 0;
  rng_state_ = 0x21f3a5c1;
  crush_phase_ = 0;
  crush_hold_ = 0;
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
    snap_ = kUnity;
    crush_phase_ = 0xffffffffUL;  // crush: take a sample right away
    partial_ = kUnity;
    phase_ = 0;
    partial_phase_ = 0;
    // The new note starts at once; the old one fades out instead of
    // dropping to zero in one sample (a click).
    tail_ = last_mix_;
    fade_ = 32767;
    if (!p.lfo_free) {
      lfo_phase_ = LfoStart(p.lfo_shape);
    }
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
  snap_ = Decay(snap_, p.snap_coefficient);
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
  x = static_cast<int32_t>(static_cast<int64_t>(x) * p.normalization >> 15);

  // Drive into a cubic soft clipper.
  x = SoftClip(x * p.drive >> 12);
  // Wave, added after the clipper: the clipper would round a triangle back
  // into a sine. Only the change from the sine is added, so BT's 2nd
  // harmonic and LT's partial stay as they are.
  if (p.wave_tri) {
    int32_t shaped = Wave(p, phase_, fundamental) - fundamental;
    x += static_cast<int32_t>(
        static_cast<int64_t>(shaped) * p.normalization >> 15);
    x = x * p.wave_gain >> 12;
    CONSTRAIN(x, -32767, 32767);
  }

  // Snap: a click of white noise. It shares the tone's 0.4 ms attack, so a
  // retrigger through an already open Tone filter does not click.
  rng_state_ = rng_state_ * 1664525UL + 1013904223UL;
  int32_t white = static_cast<int16_t>(rng_state_ >> 16);
  int32_t snap_env = static_cast<int32_t>(
      static_cast<int64_t>(snap_) * attack_ >> 45);

  int32_t v = x * env >> 15;
  v += (p.snap_noise * snap_env >> 15) * white >> 15;
  // Soft distortion (MOD 2) on the whole mix, after the envelope: the loud
  // start of a hit saturates, the quiet tail stays clean. |v| < 50000 here,
  // so v * distortion (at most 10923) fits in 32 bits. Then a gain that keeps
  // the hit as loud as the clean sound (|v| <= 32767, gain < 8192: fits).
  if (p.distortion) {
    v = SoftClip(v * p.distortion >> 12);
    v = v * p.distortion_gain >> 12;
  }
  v = ScaleMix(v);
  // Crush (METER + SHAPE 1): the mix is sampled at a lower rate and held
  // (aliasing: ring tones and grit).
  if (p.crush_increment) {
    uint32_t before = crush_phase_;
    crush_phase_ += static_cast<uint32_t>(p.crush_increment) << 1;
    if (crush_phase_ < before) {
      crush_hold_ = v;
    }
    v = crush_hold_;
  }
  if (fade_ > 0) {
    fade_ -= kFadeStep;
    if (fade_ < 0) {
      fade_ = 0;
    }
    v += tail_ * fade_ >> 15;
    CLIP(v);
  }
  last_mix_ = v;

  // Write the wave on top of a fixed lift, so the VCA word stays above zero.
  // A lift that follows each note jumps at every hit: a low-frequency step
  // that the Tone filter can't remove, heard as a click.
  if (hold_ > 0) {
    --hold_;
  } else if (lift_ < kLift) {
    ++lift_;
  }
  int32_t g = lift_ + v;
  CONSTRAIN(g, 0, 65535);
  *gain = g;

  // Filter: same amount/offset scheme as the stock envelope mode.
  int32_t f = p.tone_offset + (env * p.tone_amount >> 15);
  // Never close below a cutoff just above the pitch the oscillator plays
  // (note, snap and LFO), so Tone fully left darkens the note but
  // does not silence it. With cutoff = 48 kHz * 2^(-16.6 * (1 - f / 65535))
  // the word is linear in semitones: 329 per semitone (Q16 pitch >> 4,
  // then >> 12), up from kToneFloor at C1.
  int32_t lowest = kToneFloor + ((s >> 4) * 329 >> 12);
  if (f < lowest) {
    f = lowest;
  }
  CONSTRAIN(f, 0, 65535);
  *frequency = f;
}

}  // namespace tomline
