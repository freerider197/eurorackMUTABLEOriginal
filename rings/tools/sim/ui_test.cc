#define private public
#include "rings/ui.h"
#include "rings/settings.h"
#include "rings/cv_scaler.h"
#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "stmlib/system/system_clock.h"
#include <cstdio>
#include <string>
using namespace rings;
namespace rings { bool g_switch[2]; }
namespace stmlib { int g_saves; }
static uint16_t reverb_buffer[32768];
Settings settings; CvScaler cv_scaler; Part part; StringSynthPart string_synth; Ui ui;
static void tick(int ms) { for (int i = 0; i < ms; ++i) { ui.Poll(); ui.DoEvents(); } }
static void press(int b, int ms) { g_switch[b] = true; tick(ms); g_switch[b] = false; tick(30); }
static void both(int first, int gap, int ms) { g_switch[first] = true; tick(gap); g_switch[1-first] = true; tick(ms); g_switch[first] = false; tick(40); g_switch[1-first] = false; tick(30); }
// Right LED over one 1.6s cycle, in 20ms cells: G/Y/R full, g/y/r dim, _ off.
static std::string led_trace() {
  tick(1600 - system_clock.milliseconds() % 1600);  // align to cycle start
  std::string out;
  for (int c = 0; c < 80; ++c) {
    int r = 0, g = 0;
    for (int i = 0; i < 20; ++i) { tick(1); r += ui.leds_.red_[1]; g += ui.leds_.green_[1]; }
    char ch = '_';
    if (r > 12 && g > 12) ch = 'Y'; else if (r > 12) ch = 'R'; else if (g > 12) ch = 'G';
    else if (r > 2 && g > 2) ch = 'y'; else if (r > 2) ch = 'r'; else if (g > 2) ch = 'g';
    out += ch;
  }
  return out;
}
int main() {
  settings.Init(); part.Init(reverb_buffer); string_synth.Init(reverb_buffer);
  ui.Init(&settings, &cv_scaler, &part, &string_synth);
  const char* res[] = { "Modal", "Sympathetic", "Inharm.string", "2-op FM", "Chords", "String+reverb" };
  const char* exc[] = { "Rings", "Bow", "Blow", "Strike" };
  printf("== Right LED for each sound (short RIGHT presses), 1.6s, 20ms per char\n");
  for (int n = 0; n < 24; ++n) {
    const State& s = settings.state();
    printf("%2d %-13s %-6s mode=%d exc=%d | %s\n", n + 1, res[part.model()], exc[s.exciter], s.mode_option, s.waveform_exciter_option, led_trace().c_str());
    press(1, 80);
  }
  printf("after 24 rights back at: %s/%s\n", res[part.model()], exc[settings.state().exciter]);
  press(0, 80); printf("one LEFT from start -> %s/%s\n", res[part.model()], exc[settings.state().exciter]);
  press(1, 80);
  printf("\n== Polyphony (long LEFT 2.1s): start %ld ->", (long)part.polyphony());
  for (int i = 0; i < 4; ++i) { press(0, 2100); printf(" %ld", (long)part.polyphony()); }
  printf("   (sound unchanged: %s/%s)\n", res[part.model()], exc[settings.state().exciter]);
  printf("\n== Long RIGHT: easter egg on=%d ->", settings.state().easter_egg);
  press(1, 2100); printf(" %d, mode=%d\n", settings.state().easter_egg, settings.state().mode_option);
  for (int i = 0; i < 6; ++i) { printf("   fx %d | %s\n", settings.state().easter_egg_fx, led_trace().c_str()); press(1, 80); }
  press(1, 2100); printf("   long RIGHT again: easter egg=%d, back at %s/%s, mode=%d\n", settings.state().easter_egg, res[part.model()], exc[settings.state().exciter], settings.state().mode_option);
  printf("\n== Both buttons short (freq lock): locked=%d ->", settings.state().frequency_locked);
  both(0, 20, 400); printf(" %d, ui mode=%d (0=normal)\n", settings.state().frequency_locked, ui.mode_);
  both(0, 20, 400); printf("   again -> %d\n", settings.state().frequency_locked);
  int m = part.model(), e = settings.state().exciter;
  both(0, 20, 2300); printf("== Both long, LEFT first: ui mode=%d (0=normal), sound unchanged=%d\n", ui.mode_, m == part.model() && e == settings.state().exciter);
  press(1, 80); printf("   next short RIGHT still steps: %d\n", !(m == part.model() && e == settings.state().exciter));
  both(1, 20, 2300); printf("== Both long, RIGHT first: ui mode=%d (calibration C1 = %d)\n", ui.mode_, UI_MODE_CALIBRATION_C1);
}
