// Test stand-in for streams/drivers/adc.h: the four pots, 12-bit, with
// optional noise. Only what tomline/ui.cc uses.
#ifndef TOMLINE_TEST_STUB_ADC_H_
#define TOMLINE_TEST_STUB_ADC_H_

#include "stmlib/stmlib.h"

namespace streams {

extern int32_t sim_knob[4];   // physical positions, 0..65535
int32_t SimNoise();           // +/- ADC noise, raw counts

class Adc {
 public:
  void ScanPots() {
    int32_t v = sim_knob[index_] + SimNoise();
    v = v < 0 ? 0 : (v > 65535 ? 65535 : v);
    pots_[index_] = v & 0xfff0;  // 12-bit, left aligned
    last_ = index_;
    index_ = (index_ + 1) & 3;
  }
  uint8_t last_read_pot() const { return last_; }
  int32_t pot(uint8_t i) const { return pots_[i]; }

 private:
  uint8_t index_ = 0;
  uint8_t last_ = 0;
  uint16_t pots_[4] = { 0, 0, 0, 0 };
};

}  // namespace streams

#endif  // TOMLINE_TEST_STUB_ADC_H_
