// Test stand-in for streams/drivers/leds.h.
#ifndef TOMLINE_TEST_STUB_LEDS_H_
#define TOMLINE_TEST_STUB_LEDS_H_

#include "stmlib/stmlib.h"

namespace streams {

class Leds {
 public:
  void Init() { }
  void set(uint8_t, uint8_t, uint8_t) { }
  void Write() { }
  void Clear() { }
};

}  // namespace streams

#endif  // TOMLINE_TEST_STUB_LEDS_H_
