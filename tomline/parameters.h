// Tomline: control values to voice parameters.

#ifndef TOMLINE_PARAMETERS_H_
#define TOMLINE_PARAMETERS_H_

#include "stmlib/stmlib.h"

#include "tomline/voice.h"

namespace tomline {

enum Knob {
  KNOB_SHAPE_1,  // adc pot 0, PA2
  KNOB_MOD_1,    // adc pot 1, PA3
  KNOB_SHAPE_2,  // adc pot 2, PA1
  KNOB_MOD_2,    // adc pot 3, PA0
  KNOB_LAST
};

// main_layer: decay, tone, tune, soft distortion. The tune slot is filled
//   by ui.cc from the saved tune (FN 2 + SHAPE 1).
// second_layer: crush, unused, wave, glide (the slots where they are saved;
//   ui.cc KnobValue says which knob sets each).
// lfo_layer: LFO speed, LFO depth, tune (read through main_layer), pitch sweep.
// lfo_free: false = the LFO restarts on each trigger, true = free running.
// duck_layer: sidechain duck depth (FN 1 + SHAPE 2), release (FN 1 + MOD 2).
//   NULL = no duck.
// All values 0..65535 (knob fully counter-clockwise .. fully clockwise).
void ComputeVoiceParameters(
    uint8_t engine,
    uint8_t snap,
    const uint16_t* main_layer,
    const uint16_t* second_layer,
    const uint16_t* lfo_layer,
    uint8_t lfo_shape,
    VoiceParameters* p,
    bool lfo_free = false,
    const uint16_t* duck_layer = NULL);

// Only the wave fields (wave_tri, wave_fold, fold_gain, wave_square,
// wave_gain), from a Wave value 0..65535. ComputeVoiceParameters fills them
// the same way from second_layer[KNOB_SHAPE_2].
void ComputeWaveParameters(uint16_t wave_knob, VoiceParameters* p);

}  // namespace tomline

#endif  // TOMLINE_PARAMETERS_H_
