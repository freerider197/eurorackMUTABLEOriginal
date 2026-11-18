#ifndef RINGS_DRIVERS_CODEC_H_
#define RINGS_DRIVERS_CODEC_H_
#include "stmlib/stmlib.h"
namespace rings {
class Codec {
 public:
  typedef struct { short l; short r; } Frame;
};
}  // namespace rings
#endif
