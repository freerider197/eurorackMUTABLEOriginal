#ifndef RINGS_DRIVERS_NORMALIZATION_PROBE_H_
#define RINGS_DRIVERS_NORMALIZATION_PROBE_H_
namespace rings {
extern bool g_probe_state;
class NormalizationProbe {
 public:
  void Init() { }
  void Write(bool state) { g_probe_state = state; }
};
}  // namespace rings
#endif
