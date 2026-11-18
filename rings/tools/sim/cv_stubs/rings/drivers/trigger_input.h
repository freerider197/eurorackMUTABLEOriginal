#ifndef RINGS_DRIVERS_TRIGGER_INPUT_H_
#define RINGS_DRIVERS_TRIGGER_INPUT_H_
namespace rings {
// Test hooks: g_strum_patched = cable in STRUM; g_strum_edge = rising edge.
extern bool g_probe_state, g_strum_patched, g_strum_edge;
class TriggerInput {
 public:
  void Init() { }
  void Read() { }
  bool rising_edge() const { return g_strum_edge; }
  bool value() const { return g_strum_edge; }
  // Unpatched: the jack's switch follows the normalization probe.
  bool DummyRead() const { return g_strum_patched ? false : g_probe_state; }
};
}  // namespace rings
#endif
