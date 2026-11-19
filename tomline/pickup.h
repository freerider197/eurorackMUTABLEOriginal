// Tomline: knob pick-up. Pure code (no hardware access), so tomline/test can
// run it on a computer.
//
// When a page opens, each knob keeps its stored value until the knob is
// actually turned (by more than kPickupMove). Then the value goes to the
// knob position and follows it. The value moves at most kPickupSlew per ms,
// so a big jump is a quick 40 ms glide instead of a click. A knob that
// already sits on its value follows it straight away.

#ifndef TOMLINE_PICKUP_H_
#define TOMLINE_PICKUP_H_

#include "stmlib/stmlib.h"

namespace tomline {

const int32_t kPickupMove = 512;     // knob travel that counts as turned (0.8 %)
const int32_t kPickupNoise = 64;     // smaller changes are ADC noise
const int32_t kPickupSlew = 1638;    // per ms: the full range in 40 ms
const int32_t kPickupMaxElapsed = 2;  // ms, after a stall (flash write)

class Pickup {
 public:
  Pickup() : anchor_(0), active_(true) { }

  // Called when a page opens: keep `value` until the knob moves.
  void Hold(int32_t position, int32_t value) {
    anchor_ = position;
    active_ = Distance(position, value) <= 2 * kPickupNoise;
  }

  inline bool active() const { return active_; }

  // Moves *value toward the knob position. `elapsed` is the time since the
  // last call, in ms. Returns true if *value changed.
  bool Follow(int32_t position, int32_t elapsed, uint16_t* value) {
    if (!active_) {
      if (Distance(position, anchor_) <= kPickupMove) {
        return false;
      }
      active_ = true;
    }
    int32_t error = position - *value;
    if (Distance(position, *value) <= kPickupNoise) {
      return false;
    }
    if (elapsed > kPickupMaxElapsed) elapsed = kPickupMaxElapsed;
    int32_t step = kPickupSlew * elapsed;
    if (error > step) error = step;
    if (error < -step) error = -step;
    if (error == 0) {
      return false;
    }
    *value = static_cast<uint16_t>(*value + error);
    return true;
  }

  static inline int32_t Distance(int32_t a, int32_t b) {
    return a > b ? a - b : b - a;
  }

 private:
  int32_t anchor_;
  bool active_;
};

}  // namespace tomline

#endif  // TOMLINE_PICKUP_H_
