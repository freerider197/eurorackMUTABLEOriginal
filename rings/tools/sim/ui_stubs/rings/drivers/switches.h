#ifndef RINGS_DRIVERS_SWITCHES_H_
#define RINGS_DRIVERS_SWITCHES_H_
#include <stdint.h>
namespace rings {
const int32_t kNumSwitches = 2;
extern bool g_switch[2];
class Switches {
 public:
  void Init() { prev_[0] = prev_[1] = cur_[0] = cur_[1] = false; }
  void Debounce() { for (int i = 0; i < 2; ++i) { prev_[i] = cur_[i]; cur_[i] = g_switch[i]; } }
  bool released(int32_t i) const { return prev_[i] && !cur_[i]; }
  bool just_pressed(int32_t i) const { return !prev_[i] && cur_[i]; }
  bool pressed(int32_t i) const { return cur_[i]; }
  bool pressed_immediate(int32_t i) const { return g_switch[i]; }
  bool prev_[2], cur_[2];
};
}
#endif
