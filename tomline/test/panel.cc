// Host test for the Tomline panel: runs the firmware's own tomline/ui.cc on a
// computer, with stand-ins for the drivers (tomline/test/stub), and plays
// knob and button moves in real time (4 kHz SysTick, main loop after each).
//
// Build and run from the eurorack root:
//   g++ -O2 -Itomline/test/stub -I. -o build/tomline_panel tomline/test/panel.cc
//       tomline/voice.cc tomline/parameters.cc; ./build/tomline_panel

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "stmlib/stmlib.h"
#include "stmlib/system/system_clock.h"

// The test reads the panel's state directly.
#define private public
#include "tomline/ui.cc"
#undef private

namespace stmlib {
SystemClock system_clock;
int sim_settings_saves = 0;
}  // namespace stmlib

namespace streams {
int32_t sim_knob[4];
bool sim_button[3];
int32_t sim_noise = 0;
bool sim_can_calibrate = true;  // nothing patched, or V/oct under 1 V
int sim_offset_captures = 0, sim_calibration_saves = 0, sim_vca_offset_changes = 0;
int32_t SimNoise() {
  return sim_noise ? rand() % (2 * sim_noise + 1) - sim_noise : 0;
}
}  // namespace streams

using namespace streams;
using namespace tomline;

Adc adc;
CvScaler cv_scaler;
Ui* ui = NULL;
int failures = 0;

const int S1 = KNOB_SHAPE_1, M1 = KNOB_MOD_1, S2 = KNOB_SHAPE_2;

void Check(bool ok, const char* what) {
  printf("  %-66s %s\n", what, ok ? "OK" : "FAIL");
  failures += !ok;
}

// n ms: 4 SysTicks, each followed by a pass of the main loop.
void Ms(int n = 1) {
  for (int i = 0; i < n * 4; ++i) {
    ui->Poll();
    ui->DoEvents();
  }
}

// Turns a knob steadily to `to` over `ms`.
void Turn(int knob, int32_t to, int ms) {
  int32_t from = sim_knob[knob];
  for (int t = 1; t <= ms; ++t) {
    sim_knob[knob] = from + (to - from) * t / ms;
    Ms();
  }
  Ms(60);  // let the pot filter and the glide settle
}

void Down(int button) { sim_button[button] = true; Ms(10); }
void Up(int button) { sim_button[button] = false; Ms(10); }
void Tap(int button) { Down(button); Ms(100); Up(button); }

// Power-up with every knob at noon (second layer and LFO at their defaults:
// noise 9830, LFO speed 32768).
void Start(int32_t shape_1 = 32768, int seed = 1) {
  delete ui;
  ui = new Ui();
  srand(seed);
  sim_offset_captures = sim_calibration_saves = sim_vca_offset_changes = 0;
  stmlib::sim_settings_saves = 0;
  for (int i = 0; i < 4; ++i) sim_knob[i] = 32768;
  sim_knob[S1] = shape_1;
  std::fill(&sim_button[0], &sim_button[3], false);
  stmlib::system_clock.Init();
  ui->Init(&adc, &cv_scaler);
  Ms(100);
}

bool Near(int32_t value, int32_t target, int32_t margin = 200) {
  return value > target - margin && value < target + margin;
}

uint16_t Decay() { return ui->main_layer_[S1]; }
uint16_t Noise() { return ui->settings_.second_layer[S1]; }

// Hold METER, turn SHAPE 1 to `noise`, let go of METER.
void SetNoise(int32_t noise) {
  Down(BUTTON_METER);
  Turn(S1, noise, 800);
  Up(BUTTON_METER);
  Ms(300);
}

int main() {
  printf("Pick-up: a value waits for its knob to be turned, then goes to it\n");
  Start();
  SetNoise(5000);
  Check(Near(Noise(), 5000), "METER held, SHAPE 1 turned: noise goes to the knob");
  Check(Decay() == 32768, "back on the main page, decay kept while SHAPE 1 is not turned");
  sim_noise = 64;
  Ms(5000);
  sim_noise = 0;
  Check(Decay() == 32768, "  ... and for 5 s of knob noise (+/-4 LSB)");
  Turn(S1, 3000, 300);
  Check(Near(Decay(), 3000), "SHAPE 1 turned the other way: decay jumps to the knob");
  Start();
  SetNoise(5000);
  Turn(S1, 5000 + 400, 100);
  Check(Decay() == 32768, "a nudge under 0.8 % does not count as turning");
  Turn(S1, 20000, 300);
  Check(Near(Decay(), 20000), "SHAPE 1 turned up: decay jumps to the knob, no dead zone");
  Turn(S1, 25000, 300);
  Check(Near(Decay(), 25000), "  ... then follows it");

  printf("No click: the jump is a quick glide\n");
  Start();
  SetNoise(5000);
  {
    // A fast flick from 5000 to 60000 in 5 ms.
    int32_t largest = 0, before = Decay(), last = before;
    int glide_ms = 0;
    for (int t = 1; t <= 200; ++t) {
      if (t <= 5) sim_knob[S1] = 5000 + (60000 - 5000) * t / 5;
      Ms();
      int32_t now = Decay();
      largest = std::max(largest, Pickup::Distance(now, last));
      if (Pickup::Distance(now, 60000) > 200) glide_ms = t;
      last = now;
    }
    char text[120];
    snprintf(text, sizeof(text), "5 ms flick of SHAPE 1: steps up to %.1f %%/ms, on the knob in %d ms",
             largest * 100.0 / 65536, glide_ms);
    // Most of the time is the pot filter (about 14 ms time constant).
    Check(largest <= kPickupSlew * kPickupMaxElapsed && glide_ms <= 100 &&
          Near(Decay(), 60000), text);
  }

  printf("METER\n");
  Start();
  Down(BUTTON_METER);
  Turn(S1, 16000, 1500);
  Up(BUTTON_METER);
  Check(ui->mode_ == UI_MODE_NORMAL && !sim_offset_captures,
        "held 1.5 s while turning SHAPE 1: no offset calibration");
  Check(!ui->second_layer_latched_ && ui->layer_ == 0, "  ... no latch, back to the main page");
  Check(Near(Noise(), 16000), "  ... and the noise followed the knob");
  Start();
  Down(BUTTON_METER);
  Turn(S1, 16000, 400);
  Up(BUTTON_METER);
  Check(!ui->second_layer_latched_, "short hold while turning: no latch");
  Start();
  SetNoise(5000);
  Turn(S1, 5400, 100);
  Tap(BUTTON_METER);
  Check(ui->second_layer_latched_ && ui->layer_ == 1, "tap after a small nudge of SHAPE 1: latches");
  Check(Near(Noise(), 5000), "  ... and the noise did not move");
  Tap(BUTTON_METER);
  Check(!ui->second_layer_latched_ && ui->layer_ == 0, "second tap: unlatches");
  {
    int latched = 0;
    for (int seed = 0; seed < 20; ++seed) {
      Start(32768, seed);
      sim_noise = 64;
      SetNoise(5000);
      Tap(BUTTON_METER);
      latched += ui->second_layer_latched_;
      sim_noise = 0;
    }
    char text[120];
    snprintf(text, sizeof(text), "20 taps with knob noise (+/-4 LSB): %d latch", latched);
    Check(latched == 20, text);
  }
  Start();
  Down(BUTTON_METER);
  Ms(1500);
  Up(BUTTON_METER);
  Check(ui->mode_ == UI_MODE_NORMAL && !ui->second_layer_latched_,
        "held 1.5 s, nothing turned: does nothing");

  printf("Offset calibration (METER held 3 s)\n");
  Start();
  SetNoise(5000);  // SHAPE 1 is now away from the decay value
  Down(BUTTON_METER);
  Ms(3200);
  Up(BUTTON_METER);
  Check(ui->mode_ == UI_MODE_OFFSET_CALIBRATION && sim_offset_captures == 1,
        "held 3 s, nothing turned: starts");
  Check(sim_vca_offset_changes == 0, "  ... VCA offsets unchanged until SHAPE 1 / 2 are turned");
  Turn(S1, 20000, 300);
  Check(sim_vca_offset_changes > 0, "  ... SHAPE 1 turned: VCA offset follows");
  Tap(BUTTON_FN_1);
  Check(ui->mode_ == UI_MODE_NORMAL && sim_calibration_saves == 1, "  ... a button saves and leaves");
  Check(Decay() == 32768 && ui->settings_.engine == ENGINE_BT,
        "  ... decay and engine unchanged after it");
  sim_can_calibrate = false;
  Start();
  Down(BUTTON_METER);
  Ms(3200);
  Up(BUTTON_METER);
  sim_can_calibrate = true;
  Check(ui->mode_ == UI_MODE_NORMAL, "held 3 s with a cable at more than 1 V: does nothing");

  printf("FN 1, FN 2\n");
  Start();
  Tap(BUTTON_FN_2);
  Check(ui->settings_.snap == 2, "FN 2 tap: next snap");
  Start(10000);
  Down(BUTTON_FN_2);
  Turn(S1, 25000, 400);
  Up(BUTTON_FN_2);
  Check(ui->settings_.snap == 1, "FN 2 short hold while turning SHAPE 1: snap unchanged");
  Check(Near(ui->settings_.lfo_layer[S1], 25000), "  ... and the LFO speed followed the knob");
  Check(Decay() == 10000, "  ... decay unchanged");
  Start();
  Down(BUTTON_FN_2);
  Tap(BUTTON_METER);
  Up(BUTTON_FN_2);
  Check(ui->settings_.lfo_shape == LFO_RAMP_UP && ui->settings_.snap == 1 &&
        !ui->second_layer_latched_, "FN 2 held + METER tap: LFO wave only");
  Start();
  Down(BUTTON_METER);
  Tap(BUTTON_FN_2);
  Up(BUTTON_METER);
  Check(ui->settings_.snap == 1 && !ui->second_layer_latched_,
        "METER held + FN 2 tap: no snap, no latch");
  Start();
  Down(BUTTON_METER);
  Tap(BUTTON_FN_1);
  Up(BUTTON_METER);
  Check(ui->settings_.engine == ENGINE_BT && !ui->second_layer_latched_,
        "METER held + FN 1 tap: no engine change, no latch");
  Start();
  Tap(BUTTON_FN_1);
  Check(ui->settings_.engine == ENGINE_LT, "FN 1 tap: next engine");
  Start();
  Down(BUTTON_FN_1);
  Down(BUTTON_FN_2);
  Ms(1100);
  Up(BUTTON_FN_1);
  Up(BUTTON_FN_2);
  Check(ui->mode_ == UI_MODE_VOCT_CALIBRATION_1V && ui->settings_.engine == ENGINE_BT &&
        ui->settings_.snap == 1, "FN 1 + FN 2 held 1 s: V/oct calibration only");
  Tap(BUTTON_FN_1);
  Check(ui->mode_ == UI_MODE_NORMAL, "  ... FN 1 cancels it");

  printf("Saving\n");
  Start();
  Ms(3000);
  stmlib::sim_settings_saves = 0;
  Tap(BUTTON_FN_1);
  Ms(1500);
  bool early = stmlib::sim_settings_saves != 0;
  Ms(700);
  Check(!early && stmlib::sim_settings_saves == 1, "FN 1 tap: saved once, 2 s later");
  Start();
  stmlib::sim_settings_saves = 0;
  sim_noise = 64;
  Tap(BUTTON_METER);  // latched: the second page stays open
  Ms(60000);
  sim_noise = 0;
  Check(stmlib::sim_settings_saves == 0, "second page open 60 s, knob noise, untouched: no save");

  printf(failures ? "%d FAILED\n" : "All OK\n", failures);
  return failures ? 1 : 0;
}
