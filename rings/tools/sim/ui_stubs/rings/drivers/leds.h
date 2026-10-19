#ifndef RINGS_DRIVERS_LEDS_H_
#define RINGS_DRIVERS_LEDS_H_
#include <stdint.h>
namespace rings {
class Leds {
 public:
  void Init() { }
  void set(int32_t index, bool red, bool green) { red_[index] = red; green_[index] = green; }
  void Write() { }
  bool red_[2], green_[2];
};
}
#endif
