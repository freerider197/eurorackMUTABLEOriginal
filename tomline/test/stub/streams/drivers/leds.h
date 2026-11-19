// Test stand-in for streams/drivers/leds.h. Counts the LED colours lit at
// the last Write(), so the ADC stand-in can model LED current shifting the
// knob readings.
#ifndef TOMLINE_TEST_STUB_LEDS_H_
#define TOMLINE_TEST_STUB_LEDS_H_

#include "stmlib/stmlib.h"

namespace streams {

extern int sim_leds_lit;  // red + green channels lit at the last Write()

class Leds {
 public:
  void Init() { Clear(); }
  void set(uint8_t led, uint8_t red, uint8_t green) {
    red_[led] = red;
    green_[led] = green;
  }
  void Write() {
    int lit = 0;
    for (int i = 0; i < 8; ++i) lit += (red_[i] != 0) + (green_[i] != 0);
    sim_leds_lit = lit;
  }
  void Clear() {
    for (int i = 0; i < 8; ++i) red_[i] = green_[i] = 0;
  }

 private:
  uint8_t red_[8] = { 0 };
  uint8_t green_[8] = { 0 };
};

}  // namespace streams

#endif  // TOMLINE_TEST_STUB_LEDS_H_
