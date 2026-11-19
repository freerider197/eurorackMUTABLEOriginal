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
  9830,   // noise level 15 %
  16384,  // +1 octave level 25 %
  22938,  // unused (was sweep time)
  0       // glide off
};
const uint16_t kLfoLayerDefaults[KNOB_LAST] = {
  32768,  // LFO speed 1 Hz
  0,      // LFO depth off
  0, 0    // unused
};

static inline int32_t Abs(int32_t x) { return x < 0 ? -x : x; }

// ----------------------------------------------------------------------------

void Ui::Init(Adc* adc, CvScaler* cv_scaler) {
  adc_ = adc;
  cv_scaler_ = cv_scaler;
  leds_.Init();
  switches_.Init();

  if (!settings_storage.ParsimoniousLoad(&settings_, &version_token_) ||
      settings_.magic != kSettingsMagic) {
    settings_.engine = ENGINE_BT;
    settings_.snap = 1;
    settings_.lfo_shape = LFO_TRIANGLE;
    settings_.lfo_free = 0;
    copy(&kSecondLayerDefaults[0], &kSecondLayerDefaults[KNOB_LAST],
         &settings_.second_layer[0]);
    settings_.voct_offset = 0;
    settings_.voct_k = kDefaultVoctK;
    copy(&kLfoLayerDefaults[0], &kLfoLayerDefaults[KNOB_LAST],
         &settings_.lfo_layer[0]);
    settings_.magic = kSettingsMagic;
    // Settings saved by the firmware before the LFO: keep them. The new
    // record is written from the start of the settings pages (version 0),
    // which erases each page before its first write, as for a fresh module.
    SettingsV1 old;
    uint16_t old_token;
    if (settings_storage.ParsimoniousLoad(&old, &old_token) &&
        old.magic == kSettingsMagic) {
      settings_.engine = old.engine;
      settings_.snap = old.snap;
      copy(&old.second_layer[0], &old.second_layer[KNOB_LAST],
           &settings_.second_layer[0]);
      settings_.voct_offset = old.voct_offset;
      settings_.voct_k = old.voct_k;
    }
  }
  if (settings_.engine >= ENGINE_LAST) settings_.engine = ENGINE_BT;
  if (settings_.snap > 3) settings_.snap = 1;
  if (settings_.lfo_shape >= LFO_SHAPE_LAST) settings_.lfo_shape = LFO_TRIANGLE;
  if (settings_.lfo_free > 1) settings_.lfo_free = 0;

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
  combo_handled_ = false;
  event_read_ = event_write_ = 0;
  layer_ = 0;
  HoldKnobs();
  knob_time_ = 0;
  press_settle_ = 0;
  second_layer_latched_ = false;
  meter_held_ = false;
  fn2_held_ = false;
  fn1_with_fn2_ = false;

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
  if (button == BUTTON_FN_1) {
    // Acted on at the release, so pressing FN 1 + FN 2 for the V/oct
    // calibration (its release is ignored) does not also toggle it.
    fn1_with_fn2_ = fn2_held_;  // FN 2's own tap is cancelled (used_)
  } else if (button == BUTTON_FN_2) {
    fn2_held_ = true;
  } else if (button == BUTTON_METER) {
    if (fn2_held_) {
      // FN 2 held + METER: next LFO wave. METER's release does nothing.
      settings_.lfo_shape = (settings_.lfo_shape + 1) % LFO_SHAPE_LAST;
      SettingsChanged();
      parameters_dirty_ = true;
      long_press_handled_[button] = true;
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
        // Tapped while FN 2 was held: LFO restart on trigger / free running.
        if (short_press) {
          settings_.lfo_free = !settings_.lfo_free;
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
      if (short_press && !used_[button]) {
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

uint16_t* Ui::LayerValues(uint8_t layer) {
  if (layer == 1) return settings_.second_layer;
  if (layer == 2) return settings_.lfo_layer;
  return main_layer_;
}

// FN 2 held wins over METER; METER held or latched wins over the main layer.
void Ui::UpdateLayer(bool hold_knobs) {
  if (mode_ != UI_MODE_NORMAL) {
    return;
  }
  uint8_t layer = 0;
  if (fn2_held_) {
    layer = 2;
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
        if (down_[b]) used_[b] = true;
      }
    }
  }

  uint16_t* values = LayerValues(layer_);
  for (uint8_t i = 0; i < KNOB_LAST; ++i) {
    if (pickup_[i].Follow(pot_[i], elapsed, &values[i])) {
      parameters_dirty_ = true;
      if (layer_) {
        SettingsChanged();
      }
    }
  }
}

void Ui::ComputeParameters() {
  VoiceParameters n;
  ComputeVoiceParameters(
      settings_.engine, settings_.snap, main_layer_, settings_.second_layer,
      settings_.lfo_layer, settings_.lfo_shape, &n, settings_.lfo_free != 0);
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

  // Normal mode. Amber instead of green while the second layer is active,
  // red on the LFO layer.
  uint8_t red = layer_ ? 255 : 0;
  uint8_t green = layer_ == 1 ? 120 : (layer_ == 2 ? 0 : 255);
  leds_.set(settings_.engine, red, green);
  if ((warning_ || curve_warning_) && blink) {
    leds_.set(2, 255, 0);
  }
  if (trigger_flash_) {
    leds_.set(3, 0, 255);
  }
  if (layer_ == 2) {
    // Right column: top LED = triangle, second = ramp up. It pulses with
    // the LFO, so its speed is visible even with the depth at zero. Third
    // LED on = free running (off = restarts on each trigger).
    int32_t wave = Voice::LfoWave(lfo_phase_, settings_.lfo_shape);
    uint8_t brightness = static_cast<uint8_t>((wave + 32768) >> 8);
    leds_.set(4 + settings_.lfo_shape, brightness, 0);
    if (settings_.lfo_free) {
      leds_.set(6, 255, 0);
    }
    return;
  }
  for (uint8_t row = 0; row < 4; ++row) {
    if (3 - row <= settings_.snap) {
      leds_.set(4 + row, red, green);
    }
  }
}

}  // namespace tomline
