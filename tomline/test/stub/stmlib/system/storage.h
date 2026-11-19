// Test stand-in for stmlib/system/storage.h: nothing saved, writes counted.
#ifndef TOMLINE_TEST_STUB_STORAGE_H_
#define TOMLINE_TEST_STUB_STORAGE_H_

#include "stmlib/stmlib.h"

namespace stmlib {

extern int sim_settings_saves;

template<uint32_t last_address, uint16_t num_pages>
class Storage {
 public:
  template<typename T> static bool ParsimoniousLoad(T*, uint16_t*) { return false; }
  template<typename T> static void ParsimoniousSave(const T&, uint16_t*) {
    ++sim_settings_saves;
  }
};

}  // namespace stmlib

#endif  // TOMLINE_TEST_STUB_STORAGE_H_
