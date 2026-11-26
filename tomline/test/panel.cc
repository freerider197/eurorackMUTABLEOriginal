// Host test for the Tomline panel: runs the firmware's own tomline/ui.cc on a
// computer, with stand-ins for the drivers (tomline/test/stub), and plays
// knob and button moves in real time (4 kHz SysTick, main loop after each).
//
// Build and run from the eurorack root:
//   g++ -O2 -Itomline/test/stub -I. -o build/tomline_panel tomline/test/panel.cc
//       tomline/voice.cc tomline/parameters.cc; ./build/tomline_panel

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "stmlib/stmlib.h"
#include "stmlib/system/system_clock.h"

// The test reads the panel's state directly.
#define private public
#include "tomline/ui.cc"
#undef private

namespace stmlib {
SystemClock system_clock;
int sim_settings_saves = 0;
const void* sim_settings_record = NULL;
size_t sim_settings_record_size = 0;
}  // namespace stmlib

namespace streams {
int32_t sim_knob[4];
bool sim_button[3];
int32_t sim_noise = 0;
int sim_leds_lit = 0;
int32_t sim_led_shift = 0;
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
// crush 0, LFO speed 32768).
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

// LEVEL 2 model. The CV through LEVEL MOD 2 asks channel 2's VCA for `gain`
// (1 = unity); the sense line is the 2164 control voltage, 33 mV per dB of
// attenuation, clamped to 0 .. 3.3 V, read by a 12-bit ADC (16 counts per
// step) with +/- `noise` counts of noise. Gain 0 = nothing patched: nulled.
int32_t SenseFor(double gain, int32_t noise = 0) {
  double counts = gain > 0 ? -20.0 * log10(gain) * 0.033 / 3.3 * 65536 : 65535;
  int32_t sense = static_cast<int32_t>(counts);
  if (noise) sense += rand() % (2 * noise + 1) - noise;
  sense = std::max(0, std::min(65535, sense)) & ~15;
  return sense;
}

// n ms of audio-rate samples (8 per SysTick, 32 kHz) with LEVEL 2 at `gain`.
void Level2(double gain, int ms, int32_t noise = 0) {
  for (int i = 0; i < ms * 4; ++i) {
    for (int k = 0; k < 8; ++k) {
      ui->OnSample(false, 0, 60000, 0, 0, SenseFor(gain, noise));
    }
    ui->Poll();
    ui->DoEvents();
  }
}

// The right column as painted now: green and red of each row, top first.
void RightColumn(uint8_t* green, uint8_t* red) {
  ui->PaintLeds();
  for (int row = 0; row < 4; ++row) {
    green[row] = ui->leds_.green_[4 + row];
    red[row] = ui->leds_.red_[4 + row];
  }
}

bool RightColumnDark() {
  uint8_t g[4], r[4];
  RightColumn(g, r);
  for (int row = 0; row < 4; ++row) if (g[row] || r[row]) return false;
  return true;
}

bool Near(int32_t value, int32_t target, int32_t margin = 200) {
  return value > target - margin && value < target + margin;
}

uint16_t Decay() { return ui->main_layer_[S1]; }
uint16_t Crush() { return ui->settings_.second_layer[S1]; }

// Hold METER, turn SHAPE 1 to `crush`, let go of METER.
void SetCrush(int32_t crush) {
  Down(BUTTON_METER);
  Turn(S1, crush, 800);
  Up(BUTTON_METER);
  Ms(300);
}

int main() {
  printf("Pick-up: a value waits for its knob to be turned, then goes to it\n");
  Start();
  SetCrush(5000);
  Check(Near(Crush(), 5000), "METER held, SHAPE 1 turned: crush goes to the knob");
  Check(Decay() == 32768, "back on the main page, decay kept while SHAPE 1 is not turned");
  sim_noise = 64;
  Ms(5000);
  sim_noise = 0;
  Check(Decay() == 32768, "  ... and for 5 s of knob noise (+/-4 LSB)");
  Turn(S1, 3000, 300);
  Check(Near(Decay(), 3000), "SHAPE 1 turned the other way: decay jumps to the knob");
  Start();
  SetCrush(5000);
  Turn(S1, 5000 + 400, 100);
  Check(Decay() == 32768, "a nudge under 1.6 % does not count as turning");
  Turn(S1, 20000, 300);
  Check(Near(Decay(), 20000), "SHAPE 1 turned up: decay jumps to the knob, no dead zone");
  Turn(S1, 25000, 300);
  Check(Near(Decay(), 25000), "  ... then follows it");

  printf("LEDs that shift the knob readings (300 counts per lit colour)\n");
  sim_led_shift = 300;
  Start();
  SetCrush(5000);
  Turn(M1, 50000, 400);  // decay at noon, tone at 50000, knobs elsewhere
  {
    uint16_t main_before[4], second_before[4];
    std::copy(&ui->main_layer_[0], &ui->main_layer_[4], &main_before[0]);
    std::copy(&ui->settings_.second_layer[0], &ui->settings_.second_layer[4],
              &second_before[0]);
    bool still = true;
    for (int i = 0; i < 4; ++i) {
      Tap(BUTTON_METER);  // second page, amber
      Ms(500);
      Tap(BUTTON_METER);  // main page, green
      Ms(500);
      Down(BUTTON_FN_2);  // LFO page, red
      Ms(500);
      Up(BUTTON_FN_2);
      Ms(500);
    }
    for (int k = 0; k < 4; ++k) {
      still = still && ui->main_layer_[k] == main_before[k] &&
              ui->settings_.second_layer[k] == second_before[k];
    }
    Check(still, "page changes back and forth, knobs untouched: no value moves");
    Check(ui->settings_.snap == 2, "  ... and FN 2 holds did not step snap");
  }
  Tap(BUTTON_METER);
  Check(ui->second_layer_latched_, "METER tap still latches");
  Tap(BUTTON_METER);
  Turn(S1, 20000, 300);
  Check(Near(Decay(), 20000), "SHAPE 1 turned: decay goes to the knob");
  sim_led_shift = 0;

  printf("No click: the jump is a quick glide\n");
  Start();
  SetCrush(5000);
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
  Check(Near(Crush(), 16000), "  ... and the crush followed the knob");
  Start();
  Down(BUTTON_METER);
  Turn(S1, 16000, 400);
  Up(BUTTON_METER);
  Check(!ui->second_layer_latched_, "short hold while turning: no latch");
  Start();
  SetCrush(5000);
  Turn(S1, 5400, 100);
  Tap(BUTTON_METER);
  Check(ui->second_layer_latched_ && ui->layer_ == 1, "tap after a small nudge of SHAPE 1: latches");
  Check(Near(Crush(), 5000), "  ... and the crush did not move");
  Tap(BUTTON_METER);
  Check(!ui->second_layer_latched_ && ui->layer_ == 0, "second tap: unlatches");
  {
    int latched = 0;
    for (int seed = 0; seed < 20; ++seed) {
      Start(32768, seed);
      sim_noise = 64;
      SetCrush(5000);
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
  SetCrush(5000);  // SHAPE 1 is now away from the decay value
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
  Check(ui->settings_.snap == 3, "FN 2 tap: next snap");
  Start(10000);
  Down(BUTTON_FN_2);
  Turn(S1, 25000, 400);
  Up(BUTTON_FN_2);
  Check(ui->settings_.snap == 2, "FN 2 short hold while turning SHAPE 1: snap unchanged");
  Check(Near(ui->settings_.lfo_layer[S2], 25000), "  ... and the tune followed the knob");
  Check(Decay() == 10000, "  ... decay unchanged");
  Start();
  Down(BUTTON_FN_2);
  Tap(BUTTON_METER);
  Tap(BUTTON_FN_1);
  Up(BUTTON_FN_2);
  Check(ui->settings_.lfo_shape == LFO_TRIANGLE && !(ui->settings_.flags & kFlagLfoFree) &&
        ui->settings_.snap == 2 && ui->settings_.engine == ENGINE_BT && !ui->second_layer_latched_,
        "FN 2 held + METER / FN 1 taps: do nothing");
  Start();
  Down(BUTTON_METER);
  Tap(BUTTON_FN_2);
  Up(BUTTON_METER);
  Check(ui->settings_.lfo_shape == LFO_RAMP_UP && ui->settings_.snap == 2 &&
        !ui->second_layer_latched_, "METER held + FN 2 tap: LFO wave only");
  Start();
  Down(BUTTON_METER);
  Tap(BUTTON_FN_1);
  Up(BUTTON_METER);
  Check((ui->settings_.flags & kFlagLfoFree) && ui->settings_.engine == ENGINE_BT &&
        !ui->second_layer_latched_, "METER held + FN 1 tap: LFO free run only");
  Tap(BUTTON_METER);  // latch
  Tap(BUTTON_FN_1);
  Tap(BUTTON_FN_2);
  Check(!(ui->settings_.flags & kFlagLfoFree) && ui->settings_.lfo_shape == LFO_RAMP_UP &&
        ui->second_layer_latched_ && ui->layer_ == 1 && ui->settings_.engine == ENGINE_BT &&
        ui->settings_.snap == 2, "METER latched: FN 1 / FN 2 taps = free run / wave, page stays");
  Down(BUTTON_FN_2);
  Turn(KNOB_SHAPE_1, 50000, 400);
  Up(BUTTON_FN_2);
  Check(ui->settings_.lfo_shape == LFO_RAMP_UP && Near(ui->settings_.second_layer[KNOB_SHAPE_1], 50000),
        "  ... FN 2 held while turning SHAPE 1: crush moves, no wave change");
  Tap(BUTTON_METER);  // unlatch
  Tap(BUTTON_FN_1);
  Check(ui->settings_.engine == ENGINE_LT, "  ... back on the main page, FN 1 = engine again");
  Start();
  Down(BUTTON_FN_2);
  Down(BUTTON_FN_1);
  Ms(1100);
  Up(BUTTON_FN_1);
  Up(BUTTON_FN_2);
  Check(!(ui->settings_.flags & kFlagLfoFree), "FN 2 then FN 1 held 1 s (V/oct calibration): free run unchanged");
  Tap(BUTTON_FN_1);
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
        ui->settings_.snap == 2, "FN 1 + FN 2 held 1 s: V/oct calibration only");
  Tap(BUTTON_FN_1);
  Check(ui->mode_ == UI_MODE_NORMAL, "  ... FN 1 cancels it");

  printf("Knob map: SHAPE 2 wave; METER: MOD 1 LFO depth, MOD 2 LFO speed; FN 2: SHAPE 1 tune, MOD 1 glide\n");
  const int M2 = KNOB_MOD_2;
  Start();
  Check(ui->settings_.lfo_layer[S2] == 0, "new module: tune fully down (C1 at 0 V)");
  stmlib::sim_settings_saves = 0;
  Turn(S2, 50000, 400);
  Ms(2500);
  Check(Near(ui->settings_.second_layer[S2], 50000) && stmlib::sim_settings_saves == 1,
        "SHAPE 2 turned: wave follows, saved");
  Down(BUTTON_METER);
  Turn(M1, 20000, 400);
  Turn(M2, 45000, 400);
  Up(BUTTON_METER);
  Ms(300);
  Check(Near(ui->settings_.lfo_layer[M1], 20000), "METER + MOD 1: LFO depth follows");
  Check(Near(ui->settings_.lfo_layer[S1], 45000), "METER + MOD 2: LFO speed follows");
  Check(Near(ui->settings_.second_layer[S2], 50000) && ui->settings_.second_layer[M2] == 0 &&
        ui->settings_.second_layer[M1] == 0, "  ... wave and glide unchanged");
  Ms(2500);  // the LFO values are saved first
  stmlib::sim_settings_saves = 0;
  Down(BUTTON_METER);
  Turn(S2, 40000, 400);
  Up(BUTTON_METER);
  Ms(2500);
  Check(Near(ui->settings_.lfo_layer[KNOB_MOD_2], 40000) && stmlib::sim_settings_saves == 1,
        "METER + SHAPE 2: pitch sweep follows, saved");
  Down(BUTTON_FN_2);
  Turn(S1, 10000, 400);
  Turn(M1, 30000, 400);
  Turn(M2, 5000, 400);
  Up(BUTTON_FN_2);
  Ms(300);
  Check(Near(ui->settings_.lfo_layer[S2], 10000), "FN 2 + SHAPE 1: tune follows");
  Check(Near(ui->settings_.second_layer[M2], 30000), "FN 2 + MOD 1: glide follows");
  Check(Near(ui->settings_.lfo_layer[M1], 20000) && Near(ui->settings_.lfo_layer[S1], 45000) &&
        Decay() == 32768, "  ... FN 2 + MOD 2 does nothing; LFO and decay unchanged");
  Check(ui->parameters_.tune == static_cast<int32_t>(
            (12.0f + (ui->settings_.lfo_layer[S2] / 65535.0f - 0.5f) * 24.0f) * 65536.0f),
        "  ... the voice plays that tune");
  Turn(S1, 40000, 400);
  Check(Near(Decay(), 40000) && Near(ui->settings_.lfo_layer[S2], 10000),
        "SHAPE 1 on the main page: decay moves, tune stays");

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

  printf("Wave (SHAPE 2), crush and older saved settings\n");
  Start();
  Turn(S2, 50000, 800);
  Ms(2500);
  Check(Near(ui->settings_.second_layer[S2], 50000) && stmlib::sim_settings_saves == 1,
        "SHAPE 2 turned: wave follows the knob, saved");
  Check(ui->settings_.lfo_layer[S2] == 0, "  ... tune unchanged");
  SettingsV2 old_record;
  memset(&old_record, 0, sizeof(old_record));
  old_record.engine = ENGINE_LT;
  old_record.snap = 2;
  old_record.flags = 1;  // LFO free running, saved before the wave
  old_record.second_layer[S2] = 22938;  // the old sweep time leftover
  old_record.second_layer[S1] = 4000;
  old_record.voct_k = 12345;
  old_record.magic = kSettingsMagic;
  stmlib::sim_settings_record = &old_record;
  stmlib::sim_settings_record_size = sizeof(old_record);
  Start();
  Check(ui->settings_.second_layer[S2] == 0 && ui->settings_.engine == ENGINE_LT &&
        ui->settings_.snap == 3 && (ui->settings_.flags & kFlagLfoFree) &&
        ui->settings_.second_layer[S1] == 0 && ui->settings_.voct_k == 12345,
        "settings saved before the wave: wave and crush off, everything else kept");
  old_record.flags = kFlagWave;  // the first Wave test build: S1 was noise
  old_record.second_layer[S2] = 30000;
  Start();
  Check(ui->settings_.second_layer[S2] == 30000 && ui->settings_.second_layer[S1] == 0 &&
        !(ui->settings_.flags & kFlagLfoFree),
        "settings saved with a wave, before crush: wave kept, crush off");
  old_record.flags = kFlagWave | kFlagCrush;
  Start();
  Check(ui->settings_.second_layer[S2] == 30000 && ui->settings_.second_layer[S1] == 4000,
        "settings saved with wave and crush: both kept");
  Check(ui->settings_.snap == 3, "  ... old snap 4 ms: still 4 ms (one step up)");
  old_record.snap = 3;  // 10 ms
  Start();
  Check(ui->settings_.snap == 3, "old snap 10 ms: 4 ms, the longest now");
  old_record.snap = 0;  // 0.4 ms
  Start();
  Check(ui->settings_.snap == 1, "old snap 0.4 ms: still 0.4 ms");
  old_record.flags = kFlagWave | kFlagCrush | kFlagSnap;
  Start();
  Check(ui->settings_.snap == 0, "settings saved with snap off: off kept");
  Check(ui->settings_.lfo_layer[S2] == 0, "  ... saved before tune was saved: tune fully down");
  old_record.flags = kFlagWave | kFlagCrush | kFlagSnap | kFlagTune;
  old_record.lfo_layer[S2] = 30000;
  old_record.lfo_layer[KNOB_MOD_2] = 25000;  // leftover from an old FN 2 + MOD 2 turn
  Start();
  Check(ui->settings_.lfo_layer[S2] == 30000, "settings saved with a tune: kept");
  Check(ui->settings_.lfo_layer[KNOB_MOD_2] == 0, "  ... saved before the sweep: sweep off");
  old_record.flags = kFlagWave | kFlagCrush | kFlagSnap | kFlagTune | kFlagSweep;
  Start();
  Check(ui->settings_.lfo_layer[KNOB_MOD_2] == 25000, "settings saved with a sweep: kept");
  Check(ui->settings_.duck_layer[0] == 0 && ui->settings_.duck_layer[1] == 32768,
        "  ... saved before the duck: duck off, release at noon");
  Settings new_record;
  memset(&new_record, 0, sizeof(new_record));
  new_record.engine = ENGINE_LT;
  new_record.flags = kFlagWave | kFlagCrush | kFlagSnap | kFlagTune | kFlagSweep;
  new_record.voct_k = 12345;
  new_record.duck_layer[0] = 40000;
  new_record.duck_layer[1] = 10000;
  new_record.magic = kSettingsMagic;
  stmlib::sim_settings_record = &new_record;
  stmlib::sim_settings_record_size = sizeof(new_record);
  Start();
  Check(ui->settings_.duck_layer[0] == 40000 && ui->settings_.duck_layer[1] == 10000 &&
        ui->settings_.engine == ENGINE_LT && ui->settings_.voct_k == 12345,
        "settings saved with the duck: all kept");
  stmlib::sim_settings_record = NULL;

  printf("Duck layer (FN 1 held)\n");
  Start();
  Down(BUTTON_FN_1);
  Check(ui->layer_ == 3, "FN 1 held: duck layer");
  Turn(S2, 60000, 800);
  Turn(KNOB_MOD_2, 10000, 800);
  Up(BUTTON_FN_1);
  Ms(2500);
  Check(Near(ui->settings_.duck_layer[0], 60000) && Near(ui->settings_.duck_layer[1], 10000),
        "FN 1 held, SHAPE 2 and MOD 2 turned: depth and release follow");
  Check(ui->settings_.engine == ENGINE_BT, "  ... FN 1 let go after a turn: engine unchanged");
  Check(ui->parameters_.duck_depth > 0, "  ... the voice gets the duck depth");
  Check(stmlib::sim_settings_saves == 1, "  ... saved once");
  Check(ui->layer_ == 0 && Near(ui->settings_.second_layer[S2], 0) &&
        ui->main_layer_[KNOB_MOD_2] == 32768,
        "  ... back on the main page: wave and distortion untouched");
  Tap(BUTTON_FN_1);
  Ms(100);
  Check(ui->settings_.engine == ENGINE_LT, "FN 1 tap: engine still switches");
  Down(BUTTON_FN_1);
  Turn(S1, 0, 800);
  Turn(M1, 0, 800);
  Up(BUTTON_FN_1);
  Check(ui->main_layer_[S1] == 32768 && ui->main_layer_[M1] == 32768,
        "FN 1 held, SHAPE 1 and MOD 1 turned: nothing changes");
  uint8_t flags = ui->settings_.flags, engine = ui->settings_.engine;
  Down(BUTTON_FN_2);
  Down(BUTTON_FN_1);
  Check(ui->layer_ == 2, "FN 2 held, then FN 1: still the FN 2 page");
  Up(BUTTON_FN_1);
  Up(BUTTON_FN_2);
  Ms(100);
  Check(ui->settings_.flags == flags && ui->settings_.engine == engine,
        "  ... and the FN 1 tap does nothing there");
  Tap(BUTTON_METER);  // METER page latched
  Down(BUTTON_FN_1);
  Check(ui->layer_ == 3, "METER page, FN 1 held: duck page");
  Turn(KNOB_MOD_2, 40000, 800);
  Up(BUTTON_FN_1);
  Ms(100);
  Check(ui->layer_ == 1 && Near(ui->settings_.duck_layer[1], 40000) && ui->settings_.flags == flags,
        "  ... MOD 2 sets the release, back on the METER page, free run unchanged");
  Tap(BUTTON_FN_1);
  Ms(100);
  Check((ui->settings_.flags & kFlagLfoFree) != (flags & kFlagLfoFree),
        "METER page, FN 1 tap: LFO free run still toggles");

  printf("LEVEL 2 meter (FN 2 held, right column)\n");
  Start();
  Check(SenseToLevel(0) == 65536 && SenseToLevel(kSenseCountsPerOctave) == 32768 &&
        SenseToLevel(2 * kSenseCountsPerOctave) == 16384 && SenseToLevel(65535) == 0,
        "sense to level: 0 = unity, each 3945 counts halve it, nulled = 0");
  Down(BUTTON_FN_2);
  Level2(0, 100);
  Check(ui->layer_ == 2 && ui->level_2() == 0 && RightColumnDark(),
        "nothing patched (VCA sense nulled): level 0, right column dark");
  {
    // 0 to 5 V, with 5 V asking for unity gain.
    double worst = 0;
    bool rising = true;
    int32_t last = -1;
    for (int mv = 0; mv <= 5000; mv += 250) {
      Level2(mv / 5000.0, 30);
      int32_t level = ui->level_2();
      worst = std::max(worst, fabs(level / 65536.0 - mv / 5000.0));
      rising = rising && level > last;
      last = level;
    }
    char text[120];
    snprintf(text, sizeof(text), "0 to 5 V in 0.25 V steps: rises every step, off by %.2f %% at most",
             worst * 100);
    Check(rising && worst < 0.005, text);
  }
  {
    uint8_t g[4], r[4];
    Level2(0.5, 30);
    RightColumn(g, r);
    Check(!g[0] && !g[1] && g[2] == 255 && g[3] == 255 && !r[0],
          "2.5 V: the bottom two LEDs green, the top two dark");
    Level2(0.9, 30);
    RightColumn(g, r);
    Check(g[1] == 255 && g[2] == 255 && g[3] == 255 && g[0] > 100 && g[0] < 200 && !r[0],
          "4.5 V: three LEDs, the top one dimmer");
    Level2(0.1, 30);
    RightColumn(g, r);
    Check(!g[0] && !g[1] && !g[2] && g[3] > 70 && g[3] < 120,
          "0.5 V: the bottom LED dim, the rest dark");
    Level2(1.2, 30);
    RightColumn(g, r);
    Check(r[0] == 255 && g[1] == 255 && g[2] == 255 && g[3] == 255,
          "too much (past unity, the sense at its clamp): top LED red");
    Level2(0.94, 30);
    RightColumn(g, r);
    Check(!r[0] && g[0] > 150, "  ... just under unity: still green");
  }
  {
    // ADC noise of +/- 3 steps of the 12-bit converter.
    bool steady = true;
    for (int i = 0; i < 2 && steady; ++i) {
      double gain = i ? 0.1 : 0.5;
      Level2(gain, 30, 48);
      int32_t low = 1 << 20, high = -1;
      int low_brightness = 255, high_brightness = 0;
      for (int t = 0; t < 500; ++t) {
        Level2(gain, 1, 48);
        int32_t level = ui->level_2();
        low = std::min(low, level);
        high = std::max(high, level);
        uint8_t g[4], r[4];
        RightColumn(g, r);
        int b = i ? g[3] : g[2];
        low_brightness = std::min(low_brightness, b);
        high_brightness = std::max(high_brightness, b);
      }
      steady = steady && high - low < 328 && high_brightness - low_brightness <= 32;
    }
    Check(steady, "ADC noise (+/- 3 steps) at 2.5 V and 0.5 V: moves under 0.5 %, no LED flicker");
  }
  {
    Level2(0, 100);
    int settle = 0;
    for (int t = 1; t <= 100 && !settle; ++t) {
      Level2(1.0 * 0.99, 1);
      if (ui->level_2() > 65536 * 0.98) settle = t;
    }
    char text[120];
    snprintf(text, sizeof(text), "0 V to 5 V in one step: on the new value in %d ms", settle);
    Check(settle && settle <= 20, text);
  }
  {
    uint8_t snap = ui->settings_.snap;
    uint16_t tune = ui->settings_.lfo_layer[KNOB_SHAPE_2];
    int saves = stmlib::sim_settings_saves;
    for (int t = 0; t < 2000; t += 50) Level2((t % 1000) / 1000.0, 50);
    Up(BUTTON_FN_2);
    Ms(2500);
    Check(ui->settings_.snap == snap && ui->settings_.lfo_layer[KNOB_SHAPE_2] == tune &&
          stmlib::sim_settings_saves == saves,
          "FN 2 held 2 s with a moving CV: snap, tune and saved settings untouched");
  }
  {
    uint8_t g[4], r[4];
    Level2(0.9, 30);
    RightColumn(g, r);
    Check(ui->layer_ == 0 && !g[0] && !g[1] && g[2] && g[3] && !r[0],
          "main page with 4.5 V on LEVEL 2: right column still shows the snap");
    Tap(BUTTON_METER);
    Level2(0.9, 30);
    RightColumn(g, r);
    Check(ui->layer_ == 1 && !g[3], "  ... METER page: still the LFO, no meter");
    Tap(BUTTON_METER);
    Down(BUTTON_FN_1);
    Level2(0.9, 30);
    Check(ui->layer_ == 3 && RightColumnDark(), "  ... FN 1 page: the kick meter only (no kick: dark)");
    Up(BUTTON_FN_1);
  }

  printf("Wave CV (LEVEL 2 adds to SHAPE 2)\n");
  {
    // The voice's wave fields match ComputeWaveParameters for wave w.
    auto plays = [](int32_t w) {
      VoiceParameters n;
      ComputeWaveParameters(w, &n);
      const VoiceParameters& p = ui->parameters_;
      return p.wave_tri == n.wave_tri && p.wave_fold == n.wave_fold &&
             p.fold_gain == n.fold_gain && p.wave_square == n.wave_square &&
             p.wave_gain == n.wave_gain;
    };
    Start();
    Level2(0, 100);
    Check(ui->Wave() == 0 && plays(0), "nothing patched: the wave is the knob's (sine)");
    Level2(0.5, 50);
    Check(Near(ui->Wave(), 32768, 400) && plays(ui->Wave()),
          "knob at sine, 2.5 V: the voice plays the wave half way (folded)");
    Turn(S2, 20000, 400);
    Check(Near(ui->settings_.second_layer[S2], 20000) && Near(ui->Wave(), 52768, 500) &&
          plays(ui->Wave()), "SHAPE 2 turned to 20000: knob plus CV, the knob's value saved");
    Level2(1.0, 50);
    Check(ui->Wave() == 65535 && plays(65535) && ui->parameters_.wave_square == 32768,
          "5 V on top: stops at fully CW (square)");
    Level2(0, 100);
    Check(ui->Wave() == ui->settings_.second_layer[S2] && plays(ui->Wave()),
          "CV back to 0 V: exactly the knob again");
    int settle = 0;
    for (int t = 1; t <= 100 && !settle; ++t) {
      Level2(0.5, 1);
      if (Near(ui->Wave(), ui->settings_.second_layer[S2] + 32768, 660) && plays(ui->Wave()))
        settle = t;
    }
    char text[120];
    snprintf(text, sizeof(text), "0 V to 2.5 V in one step: the wave follows in %d ms", settle);
    Check(settle && settle <= 20, text);
    {
      int32_t low = 65535, high = 0;
      Level2(0.3, 50, 48);  // settle first
      for (int t = 0; t < 500; ++t) {
        Level2(0.3, 1, 48);
        low = std::min<int32_t>(low, ui->Wave());
        high = std::max<int32_t>(high, ui->Wave());
      }
      Check(high - low < 328, "ADC noise (+/- 3 steps) at 1.5 V: the wave moves under 0.5 %");
    }
    Ms(3000);
    int saves = stmlib::sim_settings_saves;
    uint16_t knob = ui->settings_.second_layer[S2];
    for (int t = 0; t < 3000; t += 10) Level2((t % 1000) / 1000.0, 10);
    Ms(3000);
    Check(stmlib::sim_settings_saves == saves && ui->settings_.second_layer[S2] == knob,
          "3 s of a moving CV: nothing saved, the knob's value kept");
    Level2(0.5, 50);
    Tap(BUTTON_FN_1);  // engine: a full parameter update
    Check(ui->settings_.engine == ENGINE_LT && plays(ui->Wave()) &&
          Near(ui->Wave(), knob + 32768, 500), "engine switched with the CV on: the wave keeps the CV");
  }

  printf(failures ? "%d FAILED\n" : "All OK\n", failures);
  return failures ? 1 : 0;
}
