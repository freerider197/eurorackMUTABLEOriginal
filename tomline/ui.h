// Tomline: panel handling (knobs, buttons, LEDs, calibration, settings).
//
// Control map (see docs/HARDWARE_MAP.md for where each control is wired):
//
//   SHAPE 1   decay               | with METER: noise level
//   MOD 1     tone (filter env)   | with METER: +1 octave level
//   SHAPE 2   tune, +/-24 st      | with METER: sweep time
//   MOD 2     sweep depth         | with METER: glide
//   FN 1      engine: BT / LT
//   FN 2      snap 0..3
//   METER     hold: second layer while held. Tap: latch / unlatch it.
//             Hold 1 s without turning a knob: stock offset calibration.
//   FN 1+FN 2 held 1 s: V/oct calibration (1 V, then 3 V into EXCITE 2).

#ifndef TOMLINE_UI_H_
#define TOMLINE_UI_H_

#include "stmlib/stmlib.h"

#include "streams/cv_scaler.h"
#include "streams/drivers/adc.h"
#include "streams/drivers/leds.h"
#include "streams/drivers/switches.h"

#include "tomline/parameters.h"
#include "tomline/voice.h"

namespace tomline {

enum Button {
  BUTTON_FN_1,   // switch 0, PB5
  BUTTON_FN_2,   // switch 1, PB4
  BUTTON_METER,  // switch 2, PB6
  BUTTON_LAST
};

enum UiMode {
  UI_MODE_NORMAL,
  UI_MODE_OFFSET_CALIBRATION,
  UI_MODE_VOCT_CALIBRATION_1V,
  UI_MODE_VOCT_CALIBRATION_3V
};

struct ButtonEvent {
  uint8_t button;     // BUTTON_LAST = FN 1 + FN 2 combo
  bool ignored;
  uint32_t duration;  // 0 = pressed, otherwise released after this many ms
};

const uint8_t kNumEvents = 8;

struct Settings {
  uint8_t engine;
  uint8_t snap;
  uint8_t padding[2];
  uint16_t second_layer[KNOB_LAST];
  int32_t voct_offset;  // EXCITE 2 counts at 0 V (C1)
  int32_t voct_k;       // semitones Q16 = (counts - offset) * voct_k >> 8
  uint32_t magic;
};

class Ui {
 public:
  Ui() { }
  ~Ui() { }

  void Init(streams::Adc* adc, streams::CvScaler* cv_scaler);
  void Poll();      // SysTick, 4 kHz
  void DoEvents();  // main loop

  // Called from the audio interrupt.
  inline const VoiceParameters& parameters() const { return parameters_; }
  inline bool calibrating() const { return mode_ == UI_MODE_OFFSET_CALIBRATION; }

  inline int32_t Pitch(int16_t excite_2) {
    int32_t counts = excite_2;
    if (mode_ >= UI_MODE_VOCT_CALIBRATION_1V) {
      voct_accumulator_ += counts;
      ++voct_samples_;
    }
    int32_t semitones = (counts - settings_.voct_offset) * settings_.voct_k >> 8;
    pitch_ += (semitones - pitch_) >> 5;  // ~1 ms smoothing
    return pitch_;
  }

  inline bool External(int16_t audio_1) {
    // Unpatched, IN 1 sits at +3.33 V (R59 100k from +10 V, loaded by R42
    // and R12) = about 13230 counts.
    int32_t deviation = audio_1 - 13230;
    if (deviation < 0) deviation = -deviation;
    in_deviation_ += (deviation - in_deviation_) >> 10;
    if (external_ && in_deviation_ < 1500) {
      external_ = false;
    } else if (!external_ && in_deviation_ > 2500) {
      external_ = true;
    }
    return external_;
  }

  inline void OnSample(bool trigger, uint16_t gain, uint16_t sense) {
    if (trigger) {
      trigger_flash_ = 100;  // 25 ms at 4 kHz
    }
    if (gain < 64) {
      ++idle_samples_;
      if (sense < 50000) ++idle_open_samples_;
    } else if (gain >= 8192 && gain <= 16384) {
      ++mid_samples_;
      if (sense > 20000) ++mid_closed_samples_;
    }
  }

 private:
  void PushEvent(uint8_t button, uint32_t duration, bool ignored);
  void HandleEvents();
  void OnButtonPressed(uint8_t button);
  void OnButtonReleased(uint8_t button, uint32_t duration);
  void OnButtonsCombo();
  void ReadKnobs();
  void SetSecondLayer(bool active);
  void ComputeParameters();
  void CheckSetup();
  void PaintLeds();
  void SaveSettings();

  streams::Adc* adc_;
  streams::CvScaler* cv_scaler_;
  streams::Leds leds_;
  streams::Switches switches_;

  volatile UiMode mode_;
  Settings settings_;
  uint16_t version_token_;
  bool settings_dirty_;
  uint32_t settings_change_time_;

  VoiceParameters parameters_;
  bool parameters_dirty_;

  int32_t pot_[KNOB_LAST];          // filtered positions
  uint16_t main_layer_[KNOB_LAST];
  bool caught_[2][KNOB_LAST];
  bool above_[2][KNOB_LAST];
  bool second_layer_active_;
  bool second_layer_latched_;
  bool knob_moved_while_meter_held_;

  uint32_t press_time_[BUTTON_LAST];
  bool long_press_handled_[BUTTON_LAST];
  bool combo_handled_;
  ButtonEvent events_[kNumEvents];
  volatile uint8_t event_read_;
  volatile uint8_t event_write_;
  uint8_t divider_;

  // Shared with the audio interrupt.
  volatile int32_t pitch_;
  volatile int32_t in_deviation_;
  volatile bool external_;
  volatile int32_t voct_accumulator_;
  volatile int32_t voct_samples_;
  volatile uint16_t trigger_flash_;
  volatile uint32_t idle_samples_;
  volatile uint32_t idle_open_samples_;
  volatile uint32_t mid_samples_;
  volatile uint32_t mid_closed_samples_;

  bool warning_;        // LEVEL MOD 1 not at zero
  bool curve_warning_;  // response trimmer 1 not at LIN
  uint32_t last_check_time_;
  uint16_t feedback_timer_;
  bool feedback_ok_;
  int32_t voct_1v_;
  uint8_t offset_calibration_shown_;

  DISALLOW_COPY_AND_ASSIGN(Ui);
};

}  // namespace tomline

#endif  // TOMLINE_UI_H_
