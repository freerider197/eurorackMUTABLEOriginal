// Tomline: panel handling. See ui.h for the control map.

#include "tomline/ui.h"

#include <algorithm>

#include "stmlib/system/storage.h"
#include "stmlib/system/system_clock.h"

#include "streams/gain.h"

#include "tomline/tables.h"

namespace tomline {

using namespace std;
using namespace stmlib;
using namespace streams;

// Same flash location as the stock UI settings (4 pages below the
// calibration page). The record has a different size, so the stock firmware
// ignores it and falls back to its defaults if it is flashed back.
Storage<0x801fc00, 4> settings_storage;

const uint32_t kSettingsMagic = 0x4e4c4d54;  // "TMLN"
const uint32_t kLongPress = 1000;            // ms, as the stock firmware
const uint32_t kCalibrationHold = 3000;      // ms of METER for the offsets
// EXCITE inputs: x0.2 inverting buffer into a 3.3 V ADC, 16-bit left aligned.
// 65536 * 0.2 / 3.3 = 3972 counts per volt.
const int32_t kCountsPerVolt = 3972;
const int32_t kDefaultVoctK = 12 * 65536 * 256 / kCountsPerVolt;
const uint16_t kSecondLayerDefaults[KNOB_LAST] = {
  0,      // crush off
  0,      // unused (was the +1 octave)
  0,      // wave: plain sine
  0       // glide off
};
// Settings flags. kFlagWave marks a record whose second-layer SHAPE 2 value
// is the wave. Older records hold an unused leftover there (the old sweep
// time), which would start the wave at a random place. kFlagCrush, the same
// for second-layer SHAPE 1: crush, which was the noise level. An older
// Tomline reads any flag value above 1 as "LFO restarts on trigger" and keeps
// the rest. Second-layer MOD 1 (the old +1 octave) is unused; whatever goes
// there next needs its own flag in the same way. kFlagSnap: snap 0 is off and
// 1 to 3 are 0.4, 1.5 and 4 ms; older records have 0 to 3 = 0.4, 1.5, 4 and
// 10 ms, moved up one step (10 ms becomes 4 ms). An older Tomline plays this
// record's snap one step longer. kFlagTune: LFO-layer SHAPE 2 is the tune
// (it used to be the main SHAPE 2 knob, unsaved); older records hold an
// unused leftover there, so tune starts fully down (C1 at 0 V). kFlagSweep:
// LFO-layer MOD 2 is the pitch sweep (METER + SHAPE 2); older records hold an
// unused leftover there, so the sweep starts off.
const uint8_t kFlagLfoFree = 0x01;
const uint8_t kFlagSweep = 0x08;
const uint8_t kFlagTune = 0x10;
const uint8_t kFlagSnap = 0x20;
const uint8_t kFlagCrush = 0x40;
const uint8_t kFlagWave = 0x80;
const uint16_t kLfoLayerDefaults[KNOB_LAST] = {
  32768,  // LFO speed 1 Hz
  0,      // LFO depth off
  0,      // tune fully down: 0 V on EXCITE 2 plays C1
  0       // pitch sweep off
};
const uint16_t kDuckLayerDefaults[2] = {
  0,      // duck off
  32768   // release at noon: the bass is back about 240 ms after a kick
};

static inline int32_t Abs(int32_t x) { return x < 0 ? -x : x; }

// LEVEL 2 has no ADC input of its own: through LEVEL MOD 2 it adds current to
// channel 2's VCA control node, and the op-amp there turns the total current
// into the 2164's control voltage, which the ADC reads on the sense line.
// Channel 2's VCA word stays at zero (nulled), so the current is the LEVEL 2
// CV alone. With response trimmer 2 at LIN, the control voltage is the log of
// the current: 0 V = unity gain, about 33 mV per dB of attenuation (2164
// data), clamped to 0 .. 3.3 V. 6.02 dB * 33 mV * 65536 / 3.3 V = 3945 counts
// halve the gain. Nominal: not yet checked on the module.
const int32_t kSenseCountsPerOctave = 3945;
// The top of the meter: within about 0.4 dB of unity, where the sense line
// reaches its clamp and more CV no longer reads.
const int32_t kLevel2Top = 63000;
// The wave follows the LEVEL 2 CV once it has moved this much (0.05 %), so
// ADC noise doesn't recompute it all the time.
const int32_t kWaveCvStep = 32;

// Sense counts to the gain they stand for, Q16 (65536 = unity, at 0 counts).
int32_t SenseToLevel(int32_t sense) {
  if (sense <= 0) return 65536;
  int32_t x = sense * 256 / kSenseCountsPerOctave;  // octaves down, Q8
  int32_t octaves = x >> 8;
  if (octaves >= 16) return 0;
  return lut_exp2_q30[256 - (x & 255)] >> (15 + octaves);
}

// ----------------------------------------------------------------------------

void Ui::Init(Adc* adc, CvScaler* cv_scaler) {
  adc_ = adc;
  cv_scaler_ = cv_scaler;
  leds_.Init();
  switches_.Init();

  if (!settings_storage.ParsimoniousLoad(&settings_, &version_token_) ||
      settings_.magic != kSettingsMagic) {
    settings_.engine = ENGINE_BT;
    settings_.snap = 2;  // 1.5 ms
    settings_.lfo_shape = LFO_TRIANGLE;
    settings_.flags = kFlagWave | kFlagCrush | kFlagSnap | kFlagTune | kFlagSweep;
    copy(&kSecondLayerDefaults[0], &kSecondLayerDefaults[KNOB_LAST],
         &settings_.second_layer[0]);
    settings_.voct_offset = 0;
    settings_.voct_k = kDefaultVoctK;
    copy(&kLfoLayerDefaults[0], &kLfoLayerDefaults[KNOB_LAST],
         &settings_.lfo_layer[0]);
    copy(&kDuckLayerDefaults[0], &kDuckLayerDefaults[2],
         &settings_.duck_layer[0]);
    settings_.magic = kSettingsMagic;
    // Settings saved by an older firmware: keep them. The new record is
    // written from the start of the settings pages (version 0), which erases
    // each page before its first write, as for a fresh module.
    SettingsV2 v2;
    SettingsV1 old;
    uint16_t old_token;
    if (settings_storage.ParsimoniousLoad(&v2, &old_token) &&
        v2.magic == kSettingsMagic) {
      // Before the duck: everything carries over, the duck starts off.
      settings_.engine = v2.engine;
      settings_.snap = v2.snap;
      settings_.lfo_shape = v2.lfo_shape;
      settings_.flags = v2.flags;
      copy(&v2.second_layer[0], &v2.second_layer[KNOB_LAST],
           &settings_.second_layer[0]);
      settings_.voct_offset = v2.voct_offset;
      settings_.voct_k = v2.voct_k;
      copy(&v2.lfo_layer[0], &v2.lfo_layer[KNOB_LAST],
           &settings_.lfo_layer[0]);
    } else if (settings_storage.ParsimoniousLoad(&old, &old_token) &&
        old.magic == kSettingsMagic) {
      // Before the LFO.
      settings_.engine = old.engine;
      settings_.snap = old.snap;
      copy(&old.second_layer[0], &old.second_layer[KNOB_LAST],
           &settings_.second_layer[0]);
      settings_.voct_offset = old.voct_offset;
      settings_.voct_k = old.voct_k;
      settings_.flags = 0;
    }
  }
  if (!(settings_.flags & kFlagWave)) {
    settings_.second_layer[KNOB_SHAPE_2] = 0;  // wave starts as a plain sine
  }
  if (!(settings_.flags & kFlagCrush)) {
    settings_.second_layer[KNOB_SHAPE_1] = 0;  // crush starts off
  }
  if (!(settings_.flags & kFlagSnap)) {
    if (settings_.snap > 3) settings_.snap = 1;
    settings_.snap = settings_.snap >= 2 ? 3 : settings_.snap + 1;
  }
  if (!(settings_.flags & kFlagTune)) {
    settings_.lfo_layer[KNOB_SHAPE_2] = kLfoLayerDefaults[KNOB_SHAPE_2];
  }
  if (!(settings_.flags & kFlagSweep)) {
    settings_.lfo_layer[KNOB_MOD_2] = kLfoLayerDefaults[KNOB_MOD_2];
  }
  settings_.flags = (settings_.flags & kFlagLfoFree) | kFlagWave | kFlagCrush |
      kFlagSnap | kFlagTune | kFlagSweep;
  if (settings_.engine >= ENGINE_LAST) settings_.engine = ENGINE_BT;
  if (settings_.snap > 3) settings_.snap = 2;
  if (settings_.lfo_shape >= LFO_SHAPE_LAST) settings_.lfo_shape = LFO_TRIANGLE;

  // The ADC scans one pot per ms: read all four before starting.
  for (uint8_t i = 0; i < 8; ++i) {
    adc_->ScanPots();
    for (volatile uint32_t wait = 0; wait < 20000; ++wait) { }
  }
  for (uint8_t i = 0; i < KNOB_LAST; ++i) {
    pot_[i] = adc_->pot(i);
    main_layer_[i] = pot_[i];
    press_pot_[i] = pot_[i];
  }
  calibration_pot_[0] = calibration_pot_[1] = 0;
  fill(&press_time_[0], &press_time_[BUTTON_LAST], 0);
  fill(&long_press_handled_[0], &long_press_handled_[BUTTON_LAST], false);
  fill(&down_[0], &down_[BUTTON_LAST], false);
  fill(&used_[0], &used_[BUTTON_LAST], false);
  fill(&turned_[0], &turned_[BUTTON_LAST], false);
  fill(&lfo_button_[0], &lfo_button_[BUTTON_LAST], false);
  combo_handled_ = false;
  event_read_ = event_write_ = 0;
  layer_ = 0;
  HoldKnobs();
  knob_time_ = 0;
  press_settle_ = 0;
  second_layer_latched_ = false;
  meter_held_ = false;
  fn2_held_ = false;
  fn1_held_ = false;
  fn1_with_fn2_ = false;
  duck_level_ = 0;
  level_2_sense_ = 65535 << 8;
  wave_cv_ = 0;

  mode_ = UI_MODE_NORMAL;
  settings_dirty_ = false;
  settings_change_time_ = 0;
  pitch_ = 0;
  voct_accumulator_ = 0;
  voct_samples_ = 0;
  trigger_flash_ = 0;
  lfo_phase_ = 0;
  idle_samples_ = idle_open_samples_ = mid_samples_ = mid_closed_samples_ = 0;
  warning_ = false;
  curve_warning_ = false;
  parameters_dirty_ = false;
  last_check_time_ = 0;
  feedback_timer_ = 0;
  feedback_ok_ = false;
  voct_1v_ = 0;
  offset_calibration_shown_ = 0;
  divider_ = 0;

  ComputeParameters();
}

// --- SysTick: 4 kHz ----------------------------------------------------------

void Ui::Poll() {
  ++divider_;
  if ((divider_ & 3) == 0) {
    system_clock.Tick();
    switches_.Debounce();
    uint32_t now = system_clock.milliseconds();
    for (uint8_t i = 0; i < BUTTON_LAST; ++i) {
      if (switches_.just_pressed(i)) {
        press_time_[i] = now;
        PushEvent(i, 0, false);
      } else if (switches_.released(i) && press_time_[i]) {
        uint32_t duration = now - press_time_[i];
        press_time_[i] = 0;
        // A release that ends a FN 1 + FN 2 combo is flagged so it does not
        // also count as a single press.
        PushEvent(i, duration ? duration : 1,
                  combo_handled_ && i != BUTTON_METER);
      }
    }
    // FN 1 + FN 2 held together.
    if (press_time_[BUTTON_FN_1] && press_time_[BUTTON_FN_2] && !combo_handled_) {
      uint32_t t = max(press_time_[BUTTON_FN_1], press_time_[BUTTON_FN_2]);
      if (now - t >= kLongPress) {
        combo_handled_ = true;
        PushEvent(BUTTON_LAST, 0, false);
      }
    }
    if (!press_time_[BUTTON_FN_1] && !press_time_[BUTTON_FN_2]) {
      combo_handled_ = false;
    }
    // The knobs are read with every LED off and no LED data on the wires:
    // each page lights other LEDs, and that shifted all the knob readings
    // at once. LEDs are off 1 tick in 4 (a quarter dimmer).
    leds_.Clear();
    leds_.Write();
    adc_->ScanPots();
    uint8_t i = adc_->last_read_pot();
    pot_[i] += (adc_->pot(i) - pot_[i]) >> 2;
  } else {
    PaintLeds();
    leds_.Write();
  }
  if (trigger_flash_) {
    --trigger_flash_;
  }
  if (feedback_timer_) {
    --feedback_timer_;
  }
}

// Buttons are only detected in the SysTick interrupt; everything they do
// (including flash writes) runs in the main loop, as in the stock firmware.
void Ui::PushEvent(uint8_t button, uint32_t duration, bool ignored) {
  uint8_t next = (event_write_ + 1) % kNumEvents;
  if (next == event_read_) {
    return;  // queue full: drop
  }
  events_[event_write_].button = button;
  events_[event_write_].duration = duration;
  events_[event_write_].ignored = ignored;
  event_write_ = next;
}

void Ui::HandleEvents() {
  bool was_calibrating = mode_ != UI_MODE_NORMAL;
  while (event_read_ != event_write_) {
    ButtonEvent e = events_[event_read_];
    event_read_ = (event_read_ + 1) % kNumEvents;
    if (e.button == BUTTON_LAST) {
      OnButtonsCombo();
    } else if (e.duration == 0) {
      // Two buttons down at once: neither tap does its own job.
      used_[e.button] = false;
      for (uint8_t i = 0; i < BUTTON_LAST; ++i) {
        if (i != e.button && down_[i]) {
          used_[i] = used_[e.button] = true;
        }
      }
      down_[e.button] = true;
      press_settle_ = kPickupSettle;
      long_press_handled_[e.button] = false;
      OnButtonPressed(e.button);
    } else {
      down_[e.button] = false;
      if (!e.ignored) {
        OnButtonReleased(e.button, e.duration);
      }
      if (e.button == BUTTON_FN_1) fn1_held_ = false;
      if (e.button == BUTTON_FN_2) fn2_held_ = false;
      if (e.button == BUTTON_METER) meter_held_ = false;
    }
  }
  // Back from a calibration: the knobs were used for something else, so
  // every value waits for its knob to be turned again.
  UpdateLayer(was_calibrating && mode_ == UI_MODE_NORMAL);
}

void Ui::OnButtonPressed(uint8_t button) {
  if (mode_ == UI_MODE_OFFSET_CALIBRATION) {
    // Stock behaviour: any button saves the calibration and leaves.
    cv_scaler_->SaveCalibrationData();
    mode_ = UI_MODE_NORMAL;
    offset_calibration_shown_ = 0;
    long_press_handled_[button] = true;  // swallow the release
    return;
  }
  if (mode_ != UI_MODE_NORMAL) {
    return;
  }
  // On the METER page (held or latched), FN 1 and FN 2 are the LFO buttons:
  // their taps are acted on at the release (FN 1 + FN 2 for the V/oct
  // calibration ignores both releases), and FN 2 does not open its page.
  bool meter_page = meter_held_ || second_layer_latched_;
  turned_[button] = false;
  if (button == BUTTON_FN_1) {
    fn1_with_fn2_ = fn2_held_;  // FN 1 does nothing on the FN 2 page
    lfo_button_[button] = !fn2_held_ && meter_page;
    // Held, from the main or the METER page: the duck page. Its tap still
    // does its job at the release if no knob was turned.
    fn1_held_ = !fn2_held_;
  } else if (button == BUTTON_FN_2) {
    lfo_button_[button] = meter_page;
    if (!meter_page) {
      fn2_held_ = true;
    }
  } else if (button == BUTTON_METER) {
    if (fn2_held_) {
      long_press_handled_[button] = true;  // does nothing on the FN 2 page
    } else {
      meter_held_ = true;
    }
  }
}

void Ui::OnButtonReleased(uint8_t button, uint32_t duration) {
  if (long_press_handled_[button]) {
    return;
  }
  bool short_press = duration < kLongPress;

  if (mode_ == UI_MODE_VOCT_CALIBRATION_1V ||
      mode_ == UI_MODE_VOCT_CALIBRATION_3V) {
    if (button == BUTTON_FN_2) {
      int32_t samples = voct_samples_;
      int32_t reading = samples ? voct_accumulator_ / samples : 0;
      if (mode_ == UI_MODE_VOCT_CALIBRATION_1V) {
        voct_1v_ = reading;
        mode_ = UI_MODE_VOCT_CALIBRATION_3V;
      } else {
        int32_t span = reading - voct_1v_;  // two octaves
        bool ok = span > kCountsPerVolt * 2 * 8 / 10 &&
                  span < kCountsPerVolt * 2 * 12 / 10;
        if (ok) {
          settings_.voct_k = 24 * 65536 * 256 / span;
          settings_.voct_offset = voct_1v_ - span / 2;
          SaveSettings();
        }
        feedback_ok_ = ok;
        feedback_timer_ = 4000;  // 1 s
        mode_ = UI_MODE_NORMAL;
      }
    } else if (button == BUTTON_FN_1) {
      mode_ = UI_MODE_NORMAL;  // cancel
    }
    return;
  }

  if (mode_ != UI_MODE_NORMAL) {
    return;
  }

  switch (button) {
    case BUTTON_FN_1:
      if (fn1_with_fn2_) {
        // FN 2 page: nothing.
      } else if (lfo_button_[button]) {
        // METER page: LFO restart on trigger / free running.
        if (short_press && !turned_[button]) {
          settings_.flags ^= kFlagLfoFree;
          SettingsChanged();
          parameters_dirty_ = true;
        }
      } else if (short_press && !used_[button]) {
        settings_.engine = (settings_.engine + 1) % ENGINE_LAST;
        SettingsChanged();
        parameters_dirty_ = true;
      }
      break;

    case BUTTON_FN_2:
      if (lfo_button_[button]) {
        // METER page: next LFO wave.
        if (short_press && !turned_[button]) {
          settings_.lfo_shape = (settings_.lfo_shape + 1) % LFO_SHAPE_LAST;
          SettingsChanged();
          parameters_dirty_ = true;
        }
      } else if (short_press && !used_[button]) {
        settings_.snap = (settings_.snap + 1) & 3;
        SettingsChanged();
        parameters_dirty_ = true;
      }
      break;

    case BUTTON_METER:
      if (!used_[button]) {
        if (short_press) {
          second_layer_latched_ = !second_layer_latched_;
        } else if (duration >= kCalibrationHold &&
                   cv_scaler_->can_calibrate()) {
          // Stock offset calibration: all inputs patched with cables whose
          // other end is left unplugged. SHAPE 1 / 2 only change the VCA
          // offsets once they are turned.
          cv_scaler_->CaptureAdcOffsets();
          mode_ = UI_MODE_OFFSET_CALIBRATION;
          offset_calibration_shown_ = 0;
          calibration_pot_[0] = pot_[KNOB_SHAPE_1];
          calibration_pot_[1] = pot_[KNOB_SHAPE_2];
        }
      }
      break;
  }
}

void Ui::OnButtonsCombo() {
  if (mode_ == UI_MODE_NORMAL) {
    mode_ = UI_MODE_VOCT_CALIBRATION_1V;
    voct_accumulator_ = 0;
    voct_samples_ = 0;
  }
}

// The value a knob sets on a page, NULL if the knob does nothing there. The
// values keep their old places in Settings (so saved records carry over);
// only the knobs that set them moved:
//   main:  SHAPE 2 = wave (second_layer slot 2)
//   METER: SHAPE 1 = crush, MOD 1 = LFO depth, MOD 2 = LFO speed
//   FN 2:  SHAPE 1 = tune (lfo_layer slot 2), MOD 1 = glide (second_layer
//          slot 3)
//   FN 1:  SHAPE 2 = duck depth, MOD 2 = duck release (duck_layer)
uint16_t* Ui::KnobValue(uint8_t layer, uint8_t knob) {
  if (layer == 1) {
    if (knob == KNOB_SHAPE_1) return &settings_.second_layer[KNOB_SHAPE_1];
    if (knob == KNOB_MOD_1) return &settings_.lfo_layer[KNOB_MOD_1];
    if (knob == KNOB_MOD_2) return &settings_.lfo_layer[KNOB_SHAPE_1];
    return &settings_.lfo_layer[KNOB_MOD_2];  // SHAPE 2: pitch sweep
  }
  if (layer == 2) {
    if (knob == KNOB_SHAPE_1) return &settings_.lfo_layer[KNOB_SHAPE_2];
    if (knob == KNOB_MOD_1) return &settings_.second_layer[KNOB_MOD_2];
    return NULL;
  }
  if (layer == 3) {
    if (knob == KNOB_SHAPE_2) return &settings_.duck_layer[0];
    if (knob == KNOB_MOD_2) return &settings_.duck_layer[1];
    return NULL;
  }
  if (knob == KNOB_SHAPE_2) return &settings_.second_layer[KNOB_SHAPE_2];
  return &main_layer_[knob];
}

// FN 2 held wins over FN 1 held, FN 1 held over METER; METER held or latched
// wins over the main layer.
void Ui::UpdateLayer(bool hold_knobs) {
  if (mode_ != UI_MODE_NORMAL) {
    return;
  }
  uint8_t layer = 0;
  if (fn2_held_) {
    layer = 2;
  } else if (fn1_held_) {
    layer = 3;
  } else if (meter_held_ || second_layer_latched_) {
    layer = 1;
  }
  if (layer != layer_ || hold_knobs) {
    layer_ = layer;
    HoldKnobs();
  }
}

// A page opens: every value stays where it is until its knob is turned.
void Ui::HoldKnobs() {
  for (uint8_t i = 0; i < KNOB_LAST; ++i) {
    pickup_[i].Hold(pot_[i]);
  }
}

void Ui::SettingsChanged() {
  settings_dirty_ = true;
  settings_change_time_ = system_clock.milliseconds();
}

// --- Main loop ---------------------------------------------------------------

void Ui::ReadKnobs() {
  uint32_t now = system_clock.milliseconds();
  int32_t elapsed = now - knob_time_;
  knob_time_ = now;

  // A knob turned while a button is down cancels that button's tap. The
  // positions are taken once the readings have settled after the press.
  if (press_settle_ > 0) {
    press_settle_ -= elapsed;
    copy(&pot_[0], &pot_[KNOB_LAST], &press_pot_[0]);
  }
  for (uint8_t i = 0; i < KNOB_LAST; ++i) {
    if (Pickup::Distance(pot_[i], press_pot_[i]) > kPickupMove) {
      for (uint8_t b = 0; b < BUTTON_LAST; ++b) {
        if (down_[b]) used_[b] = turned_[b] = true;
      }
    }
  }

  for (uint8_t i = 0; i < KNOB_LAST; ++i) {
    uint16_t* value = KnobValue(layer_, i);
    if (value && pickup_[i].Follow(pot_[i], elapsed, value)) {
      parameters_dirty_ = true;
      if (layer_ || i == KNOB_SHAPE_2) {  // saved values
        SettingsChanged();
      }
    }
  }
}

// The wave the voice plays: SHAPE 2 plus the LEVEL 2 CV, up to fully CW.
// Only the knob's value is saved.
uint16_t Ui::Wave() const {
  return min(static_cast<int32_t>(settings_.second_layer[KNOB_SHAPE_2]) +
             wave_cv_, static_cast<int32_t>(65535));
}

// The LEVEL 2 CV moved: send the voice the new wave (its wave fields only).
void Ui::FollowWaveCv() {
  int32_t cv = min(level_2(), static_cast<int32_t>(65535));
  if (Abs(cv - wave_cv_) <= kWaveCvStep && !(cv == 0 && wave_cv_)) {
    return;
  }
  wave_cv_ = cv;
  if (parameters_dirty_) {
    return;  // ComputeParameters is about to do it
  }
  VoiceParameters n;
  ComputeWaveParameters(Wave(), &n);
  // Each 32-bit store is atomic for the interrupt.
  volatile VoiceParameters* p = &parameters_;
  p->wave_tri = n.wave_tri;
  p->wave_fold = n.wave_fold;
  p->fold_gain = n.fold_gain;
  p->wave_square = n.wave_square;
  p->wave_gain = n.wave_gain;
}

void Ui::ComputeParameters() {
  VoiceParameters n;
  // The voice reads tune from main SHAPE 2's slot; it is saved in the LFO
  // layer now (set by FN 2 + SHAPE 1).
  uint16_t main_layer[KNOB_LAST];
  copy(&main_layer_[0], &main_layer_[KNOB_LAST], &main_layer[0]);
  main_layer[KNOB_SHAPE_2] = settings_.lfo_layer[KNOB_SHAPE_2];
  uint16_t second_layer[KNOB_LAST];
  copy(&settings_.second_layer[0], &settings_.second_layer[KNOB_LAST],
       &second_layer[0]);
  second_layer[KNOB_SHAPE_2] = Wave();
  ComputeVoiceParameters(
      settings_.engine, settings_.snap, main_layer, second_layer,
      settings_.lfo_layer, settings_.lfo_shape, &n,
      (settings_.flags & kFlagLfoFree) != 0, settings_.duck_layer);
  // Copy field by field: each 32-bit store is atomic for the interrupt.
  volatile int32_t* dst = reinterpret_cast<volatile int32_t*>(&parameters_);
  const int32_t* src = reinterpret_cast<const int32_t*>(&n);
  for (size_t i = 0; i < sizeof(VoiceParameters) / sizeof(int32_t); ++i) {
    dst[i] = src[i];
  }
}

void Ui::CheckSetup() {
  // LEVEL MOD 1 must be fully counter-clockwise: with the VCA word at zero,
  // channel 1's VCA control voltage must sit at the "nulled" end (the stock
  // calibration test reads > 59000 there).
  if (idle_samples_ > 4096) {
    warning_ = idle_open_samples_ > idle_samples_ / 10 * 9;
  }
  // Response trimmer 1 must be fully clockwise (LIN): at VCA words between
  // -12 and -6 dB, the lin law keeps the control voltage under 0.5 V. Toward
  // EXP, the same words push it to the 3.3 V clamp.
  if (mid_samples_ > 1024) {
    curve_warning_ = mid_closed_samples_ > mid_samples_ / 2;
  }
  idle_samples_ = idle_open_samples_ = mid_samples_ = mid_closed_samples_ = 0;
}

int32_t Ui::level_2() const {
  return SenseToLevel(level_2_sense_ >> 8);
}

void Ui::SaveSettings() {
  settings_storage.ParsimoniousSave(settings_, &version_token_);
  settings_dirty_ = false;
}

void Ui::DoEvents() {
  HandleEvents();
  uint32_t now = system_clock.milliseconds();
  if (mode_ == UI_MODE_OFFSET_CALIBRATION) {
    // Stock behaviour: SHAPE 1 / SHAPE 2 null the VCA bleed of channel 1 / 2.
    for (uint8_t channel = 0; channel < 2; ++channel) {
      int32_t position = pot_[channel * 2];
      int32_t min = kDefaultOffset >> 1;
      int32_t max = 3 * min + 256;
      if (Abs(position - calibration_pot_[channel]) > 256) {
        calibration_pot_[channel] = position;
        cv_scaler_->set_dac_offset(channel, min + ((max - min) * position >> 16));
        offset_calibration_shown_ |= 1 << channel;
      }
    }
    return;
  }
  if (mode_ == UI_MODE_VOCT_CALIBRATION_1V ||
      mode_ == UI_MODE_VOCT_CALIBRATION_3V) {
    // Keep a fresh 0.25 s average of EXCITE 2.
    if (voct_samples_ > 7500) {
      voct_accumulator_ = voct_accumulator_ / voct_samples_ * 64;
      voct_samples_ = 64;
    }
    return;
  }
  ReadKnobs();
  FollowWaveCv();
  if (parameters_dirty_) {
    parameters_dirty_ = false;
    ComputeParameters();
  }
  if (now - last_check_time_ >= 500) {
    last_check_time_ = now;
    CheckSetup();
  }
  if (settings_dirty_ && now - settings_change_time_ > 2000) {
    SaveSettings();
  }
}

// --- LEDs --------------------------------------------------------------------
// leds_.set(column * 4 + row, red, green). Column 0 = left (channel 1),
// column 1 = right (channel 2). Row 0 = top, row 3 = bottom.

void Ui::PaintLeds() {
  leds_.Clear();
  uint32_t ms = system_clock.milliseconds();
  bool blink = (ms & 256) != 0;

  if (feedback_timer_) {
    for (uint8_t i = 0; i < 8; ++i) {
      leds_.set(i, feedback_ok_ ? 0 : 255, feedback_ok_ ? 255 : 0);
    }
    return;
  }

  switch (mode_) {
    case UI_MODE_OFFSET_CALIBRATION:
      // Stock pattern: column 1 full, column 2 top and bottom; red until the
      // VCA is nulled, then green.
      for (uint8_t channel = 0; channel < 2; ++channel) {
        uint8_t red = 0, green = 255;
        if (offset_calibration_shown_ & (1 << channel)) {
          bool nulled = cv_scaler_->raw_gain_sample(channel) > 59000;
          red = nulled ? 0 : 255;
          green = nulled ? 255 : 0;
        }
        uint8_t pattern = channel == 0 ? 15 : 9;
        for (uint8_t row = 0; row < 4; ++row) {
          bool on = pattern & (1 << row);
          leds_.set(channel * 4 + row, on ? red : 0, on ? green : 0);
        }
      }
      return;

    case UI_MODE_VOCT_CALIBRATION_1V:
    case UI_MODE_VOCT_CALIBRATION_3V:
      {
        uint8_t first = mode_ == UI_MODE_VOCT_CALIBRATION_1V ? 0 : 2;
        if (blink) {
          leds_.set(4 + first, 0, 255);
          leds_.set(4 + first + 1, 0, 255);
        }
        leds_.set(first, 255, 255);
        leds_.set(first + 1, 255, 255);
      }
      return;

    default:
      break;
  }

  // Normal mode. Amber instead of green on the METER page, red on the FN 2
  // page, yellow on the duck page (FN 1 held).
  uint8_t red = layer_ ? 255 : 0;
  uint8_t green = layer_ == 1 ? 120 : (layer_ == 2 ? 0 : 255);
  leds_.set(settings_.engine, red, green);
  if ((warning_ || curve_warning_) && blink) {
    leds_.set(2, 255, 0);
  }
  if (trigger_flash_) {
    leds_.set(3, 0, 255);
  }
  if (layer_ == 1) {
    // METER page, right column: the LFO. Top LED = triangle, second = ramp
    // up; it pulses with the LFO, so its speed shows even at zero amount.
    // Third LED on = free running (off = restarts on each trigger).
    int32_t wave = Voice::LfoWave(lfo_phase_, settings_.lfo_shape);
    uint8_t brightness = static_cast<uint8_t>((wave + 32768) >> 8);
    leds_.set(4 + settings_.lfo_shape, brightness, brightness * 120 >> 8);
    if (settings_.flags & kFlagLfoFree) {
      leds_.set(6, red, green);
    }
    return;
  }
  if (layer_ == 2) {
    // FN 2 page (tune and glide), right column: a meter of the CV on LEVEL 2,
    // which moves the wave, so the patch can be checked. Green from the bottom, each LED a quarter of the
    // readable range, the top one partly lit dimmer, in the LED driver's
    // eighths (31, 63 .. 255). The top LED turns red when the reading is at
    // the end of its range (unity gain or more).
    int32_t level = level_2();
    for (uint8_t row = 0; row < 4; ++row) {
      int32_t eighths = (level - (3 - row) * 16384 + 1024) >> 11;
      if (eighths <= 0) continue;
      leds_.set(4 + row, 0, eighths >= 8 ? 255 : eighths * 32 - 1);
    }
    if (level >= kLevel2Top) {
      leds_.set(4, 255, 0);
    }
    return;
  }
  if (layer_ == 3) {
    // Right column: a meter of the kick heard on IN 2, from the bottom, in
    // red. It moves even with the depth at zero, so the patch can be checked.
    int32_t level = duck_level_;
    for (uint8_t row = 0; row < 4; ++row) {
      if (level > (3 - row) * 8192 + 1024) {
        leds_.set(4 + row, 255, 0);
      }
    }
    return;
  }
  for (uint8_t row = 0; row < 4; ++row) {
    if (3 - row < settings_.snap) {
      leds_.set(4 + row, red, green);
    }
  }
}

}  // namespace tomline
