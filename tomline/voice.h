// Tomline: bass tom / low tom voice for the Mutable Instruments Streams
// hardware. Alternative firmware, based on the Streams source code
// (Copyright 2014 Emilie Gillet, MIT license).
//
// The voice runs inside the ADC interrupt at 31.088 kHz. It outputs two
// numbers per sample, exactly like the stock Streams processors:
//
//   gain      DAC8552 word for the VCA. With the channel's response trimmer
//             fully clockwise (LIN), gain = word / 32767 (see streams/gain.h),
//             and with IN unpatched the VCA input is +3.33 V, so OUT 1 =
//             3.33 V * word / 32767, from 0 V up to 6.67 V.
//   frequency Filter control word, written as pwm.Write(0, 65535 - frequency).
//             65535 = filter open, 0 = filter closed.
//
// The VCA can only scale a positive source, so the wave is written on top of
// a fixed lift: OUT 1 sits at mid scale (about 3.33 V) and the wave swings
// around it. The lift never moves while notes play, so it adds no thump; an
// AC-coupled input downstream removes it. At power-up OUT 1 stays at 0 V for
// 1 s, then rises to mid scale over about 1 s.

#ifndef TOMLINE_VOICE_H_
#define TOMLINE_VOICE_H_

#include "stmlib/stmlib.h"

namespace tomline {

enum Engine {
  ENGINE_BT = 0,  // Bass tom
  ENGINE_LT = 1,  // Low tom
  ENGINE_LAST
};

enum LfoShape {
  LFO_TRIANGLE = 0,
  LFO_RAMP_UP = 1,
  LFO_SHAPE_LAST
};

// Values computed in the main loop (control rate), read by the interrupt.
// Each field is 32 bits, so every individual write is atomic.
struct VoiceParameters {
  int32_t engine;
  int32_t amp_coefficient;      // Q31 per-sample decay multiplier
  int32_t partial_coefficient;  // Q31
  int32_t noise_coefficient;    // Q31
  int32_t snap_coefficient;     // Q31
  int32_t glide_k;              // Q31, 0 = no glide
  int32_t tune;                 // semitones, Q16
  int32_t snap_depth;           // semitones, Q16
  int32_t octave_level;         // Q15
  int32_t harmonic_level;       // Q15 (2nd harmonic of BT, partial of LT)
  int32_t normalization;        // Q15
  int32_t noise_level;          // Q15
  int32_t snap_noise;           // Q15
  int32_t noise_k1;             // Q15 one-pole coefficient
  int32_t noise_k2;             // Q15 one-pole coefficient
  int32_t drive;                // Q12 pre-gain into the soft clipper
  int32_t distortion;           // Q12 gain into the output soft clipper, 0 = off
  int32_t wave_tri;             // Q15 (32768 = full), sine -> triangle, 0 = plain sine
  int32_t wave_fold;            // Q15, triangle -> folded sine
  int32_t fold_gain;            // Q12, 1 to 2: how far the top folds back
  int32_t wave_square;          // Q15, folded -> square
  int32_t tone_amount;          // 0..65535, as streams/meta_parameters.h
  int32_t tone_offset;          // 0..65535
  int32_t lfo_increment;        // pitch LFO phase increment per sample
  int32_t lfo_depth;            // semitones, Q16, 0 = off
  int32_t lfo_shape;            // LfoShape
  int32_t lfo_free;             // 1 = free running, 0 = restarts on each trigger
};

class Voice {
 public:
  Voice() { }
  ~Voice() { }

  void Init();

  // trigger: rising edge seen on EXCITE 1 this sample.
  // pitch: target pitch in semitones above C1, Q16 (V/oct + tune).
  void Process(
      const VoiceParameters& p,
      bool trigger,
      int32_t pitch,
      uint16_t* gain,
      uint16_t* frequency);

  inline int32_t envelope() const { return amp_; }
  inline uint32_t lfo_phase() const { return lfo_phase_; }

  // Where a trigger restarts the LFO: the middle of the wave (no pitch
  // offset), going up.
  static inline uint32_t LfoStart(int32_t shape) {
    return shape == LFO_RAMP_UP ? 0x80000000UL : 0x40000000UL;
  }

  // Pitch LFO wave, -32768 .. 32767. Triangle: rises for the first half of
  // the cycle, falls for the second. Ramp up: rises, then jumps back down.
  static inline int32_t LfoWave(uint32_t phase, int32_t shape) {
    if (shape == LFO_RAMP_UP) {
      return static_cast<int32_t>(phase >> 16) - 32768;
    }
    uint32_t t = phase < 0x80000000UL ? phase : ~phase;
    return static_cast<int32_t>(t >> 15) - 32768;
  }

  // Cubic soft clipper, (3x - x^3) / 2 on x clipped to +/-1 (Q15): slope
  // 1.5 at zero, flat at full scale.
  static inline int32_t SoftClip(int32_t x) {
    CONSTRAIN(x, -32767, 32767);
    int32_t cube = ((x * x) >> 15) * x >> 15;
    x = (3 * x - cube) >> 1;
    CONSTRAIN(x, -32767, 32767);
    return x;
  }

  // 0.95 * v, clipped to +/-32767. 64-bit: with noise at 100 % on a hard hit,
  // the mix can reach about 82000, and 82000 * 31130 overflows 32 bits.
  static inline int32_t ScaleMix(int32_t v) {
    int32_t scaled = static_cast<int32_t>(static_cast<int64_t>(v) * 31130 >> 15);
    CLIP(scaled);
    return scaled;
  }

 private:

  int32_t semitones_;
  bool gliding_;

  uint32_t phase_;
  uint32_t partial_phase_;
  uint32_t lfo_phase_;  // restarted by triggers unless lfo_free

  int32_t amp_;       // Q30
  int32_t attack_;    // Q30
  int32_t snap_;      // Q30
  int32_t noise_;     // Q30
  int32_t partial_;   // Q30
  int32_t lift_;      // VCA word the wave swings around
  int32_t hold_;      // power-up samples left at 0 V
  int32_t last_mix_;  // Q15, previous output sample (before the lift)
  int32_t tail_;      // Q15, old note's last sample, faded out on retrigger
  int32_t fade_;      // Q15, tail level, 0 = no tail

  uint32_t rng_state_;
  int32_t noise_lp1_;
  int32_t noise_lp2_;

  DISALLOW_COPY_AND_ASSIGN(Voice);
};

}  // namespace tomline

#endif  // TOMLINE_VOICE_H_
