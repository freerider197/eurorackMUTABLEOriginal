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
// a lift ("bias") that follows the sound's own peak level and returns to 0 V
// when the note has died out.

#ifndef TOMLINE_VOICE_H_
#define TOMLINE_VOICE_H_

#include "stmlib/stmlib.h"

namespace tomline {

enum Engine {
  ENGINE_BT = 0,  // Bass tom
  ENGINE_LT = 1,  // Low tom
  ENGINE_LAST
};

// Values computed in the main loop (control rate), read by the interrupt.
// Each field is 32 bits, so every individual write is atomic.
struct VoiceParameters {
  int32_t engine;
  int32_t amp_coefficient;      // Q31 per-sample decay multiplier
  int32_t partial_coefficient;  // Q31
  int32_t noise_coefficient;    // Q31
  int32_t sweep_coefficient;    // Q31
  int32_t snap_coefficient;     // Q31
  int32_t glide_k;              // Q31, 0 = no glide
  int32_t tune;                 // semitones, Q16
  int32_t sweep_depth;          // semitones, Q16
  int32_t snap_depth;           // semitones, Q16
  int32_t octave_level;         // Q15
  int32_t harmonic_level;       // Q15 (2nd harmonic of BT, partial of LT)
  int32_t normalization;        // Q15
  int32_t noise_level;          // Q15
  int32_t snap_noise;           // Q15
  int32_t noise_k1;             // Q15 one-pole coefficient
  int32_t noise_k2;             // Q15 one-pole coefficient
  int32_t drive;                // Q12 pre-gain into the soft clipper
  int32_t tone_amount;          // 0..65535, as streams/meta_parameters.h
  int32_t tone_offset;          // 0..65535
};

class Voice {
 public:
  Voice() { }
  ~Voice() { }

  void Init();

  // trigger: rising edge seen on EXCITE 1 this sample.
  // pitch: target pitch in semitones above C1, Q16 (V/oct + tune).
  // external: IN 1 is patched; shape the external signal instead.
  void Process(
      const VoiceParameters& p,
      bool trigger,
      int32_t pitch,
      bool external,
      uint16_t* gain,
      uint16_t* frequency);

  inline int32_t envelope() const { return amp_; }

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

  int32_t amp_;       // Q30
  int32_t attack_;    // Q30
  int32_t sweep_;     // Q30
  int32_t snap_;      // Q30
  int32_t noise_;     // Q30
  int32_t partial_;   // Q30
  int32_t bias_;      // Q15

  uint32_t rng_state_;
  int32_t noise_lp1_;
  int32_t noise_lp2_;

  DISALLOW_COPY_AND_ASSIGN(Voice);
};

}  // namespace tomline

#endif  // TOMLINE_VOICE_H_
