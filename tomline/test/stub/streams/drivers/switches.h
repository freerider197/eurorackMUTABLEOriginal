// Test stand-in for streams/drivers/switches.h, with the same debounce.
#ifndef TOMLINE_TEST_STUB_SWITCHES_H_
#define TOMLINE_TEST_STUB_SWITCHES_H_

#include "stmlib/stmlib.h"

namespace streams {

extern bool sim_button[3];  // true = held down

class Switches {
 public:
  void Init() { for (int i = 0; i < 3; ++i) state_[i] = 0xff; }
  void Debounce() {
    for (int i = 0; i < 3; ++i) {
      state_[i] = (state_[i] << 1) | (sim_button[i] ? 0 : 1);
    }
  }
  bool released(uint8_t i) const { return state_[i] == 0x7f; }
  bool just_pressed(uint8_t i) const { return state_[i] == 0x80; }
  bool pressed(uint8_t i) const { return state_[i] == 0x00; }

 private:
  uint8_t state_[3];
};

}  // namespace streams

#endif  // TOMLINE_TEST_STUB_SWITCHES_H_
