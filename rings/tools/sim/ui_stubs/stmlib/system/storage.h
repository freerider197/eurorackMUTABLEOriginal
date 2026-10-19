#ifndef STMLIB_SYSTEM_STORAGE_H_
#define STMLIB_SYSTEM_STORAGE_H_
#include <stdint.h>
#include <cstring>
namespace stmlib {
extern int g_saves;
template<int N> class Storage {
 public:
  template<typename T> bool ParsimoniousLoad(T* d, uint16_t* tok) { return false; }
  template<typename T> void ParsimoniousSave(const T& d, uint16_t* tok) { ++g_saves; }
};
}
#endif
