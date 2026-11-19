// Test stand-in for stmlib/system/storage.h: nothing saved, writes counted.
// A test can set sim_settings_record to a record that a load of the same size
// then returns.
#ifndef TOMLINE_TEST_STUB_STORAGE_H_
#define TOMLINE_TEST_STUB_STORAGE_H_

#include <cstring>

#include "stmlib/stmlib.h"

namespace stmlib {

extern int sim_settings_saves;
extern const void* sim_settings_record;
extern size_t sim_settings_record_size;

template<uint32_t last_address, uint16_t num_pages>
class Storage {
 public:
  template<typename T> static bool ParsimoniousLoad(T* data, uint16_t*) {
    if (!sim_settings_record || sim_settings_record_size != sizeof(T)) {
      return false;
    }
    memcpy(data, sim_settings_record, sizeof(T));
    return true;
  }
  template<typename T> static void ParsimoniousSave(const T&, uint16_t*) {
    ++sim_settings_saves;
  }
};

}  // namespace stmlib

#endif  // TOMLINE_TEST_STUB_STORAGE_H_
