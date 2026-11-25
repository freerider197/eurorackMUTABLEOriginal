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

// main_layer: SHAPE 1 decay, MOD 1 tone, tune, MOD 2 soft distortion. The
//   tune slot is filled by ui.cc from FN 2 + SHAPE 2 (the main SHAPE 2 knob
//   sets the LFO depth).
// second_layer: crush, unused, wave, glide.
// lfo_layer: SHAPE 1 LFO speed, MOD 1 LFO depth (SHAPE 2 tune, read through
//   main_layer; MOD 2 unused).
// lfo_free: false = the LFO restarts on each trigger, true = free running.
// All values 0..65535 (knob fully counter-clockwise .. fully clockwise).
void ComputeVoiceParameters(
    uint8_t engine,
    uint8_t snap,
    const uint16_t* main_layer,
    const uint16_t* second_layer,
    const uint16_t* lfo_layer,
    uint8_t lfo_shape,
    VoiceParameters* p,
    bool lfo_free = false);

}  // namespace tomline

#endif  // TOMLINE_PARAMETERS_H_
