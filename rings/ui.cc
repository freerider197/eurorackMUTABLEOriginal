// Copyright 2015 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// User interface.

#include "rings/ui.h"

#include <algorithm>

#include "stmlib/system/system_clock.h"

#include "rings/cv_scaler.h"
#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"

namespace rings {

const int32_t kLongPressDuration = 2000;
const int32_t kMediumPressDuration = 200;

// The sounds are the 6 resonators x 4 exciters, in this order: each
// resonator with the Rings exciter, then Elements bow, blow and strike.
const int32_t kNumExciters = 4;
const int32_t kNumSounds = RESONATOR_MODEL_LAST * kNumExciters;
const int32_t kNumEasterEggFx = FX_LAST;

using namespace std;
using namespace stmlib;

void Ui::Init(
    Settings* settings,
    CvScaler* cv_scaler,
    Part* part,
    StringSynthPart* string_synth) {
  leds_.Init();
  switches_.Init();
  
  settings_ = settings;
  cv_scaler_ = cv_scaler;
  part_ = part;
  string_synth_ = string_synth;
  
  mode_ = UI_MODE_NORMAL;
  if (switches_.pressed_immediate(1)) {
    State* state = settings_->mutable_state();
    if (state->color_blind == 1) {
      state->color_blind = 0; 
    } else {
      state->color_blind = 1; 
    }
    settings_->Save();
  }
  
  part_->set_polyphony(settings_->state().polyphony);
  part_->set_model(static_cast<ResonatorModel>(settings_->state().model));
  string_synth_->set_polyphony(settings_->state().polyphony);
  string_synth_->set_fx(
      static_cast<FxType>(settings_->state().easter_egg_fx));
  mode_ = UI_MODE_NORMAL;
}

void Ui::SaveState() {
  settings_->mutable_state()->polyphony = part_->polyphony();
  settings_->mutable_state()->model = part_->model();
  settings_->Save();
}

void Ui::StepSound(int32_t direction) {
  State* state = settings_->mutable_state();
  if (state->easter_egg) {
    state->easter_egg_fx = (state->easter_egg_fx + kNumEasterEggFx + \
        direction) % kNumEasterEggFx;
    string_synth_->set_fx(static_cast<FxType>(state->easter_egg_fx));
  } else {
    int32_t sound = part_->model() * kNumExciters + state->exciter;
    sound = (sound + kNumSounds + direction) % kNumSounds;
    part_->set_model(static_cast<ResonatorModel>(sound / kNumExciters));
    state->exciter = sound % kNumExciters;
    settings_->UpdateModeOptions();
  }
  SaveState();
}

void Ui::ToggleEasterEgg() {
  State* state = settings_->mutable_state();
  state->easter_egg = !state->easter_egg;
  settings_->UpdateModeOptions();
  SaveState();
}

void Ui::StepPolyphony() {
  int32_t polyphony = part_->polyphony() % 4 + 1;
  part_->set_polyphony(polyphony);
  string_synth_->set_polyphony(polyphony);
  SaveState();
}

// Right LED: where you are in the list of sounds.
// - Color (and motion) = resonator. The 2-op FM is the only one that
//   "breathes".
// - Number of short blinks in each 1.6s cycle = exciter: none for Rings,
//   1 for bow, 2 for blow, 3 for strike.
// In the easter egg, the color is the FX and the LED flickers quickly.
void Ui::RenderSoundLed(uint32_t ms, uint8_t pwm_counter) {
  const State& state = settings_->state();
  uint32_t t = ms % 1600;
  bool phase = (t / 400) & 1;
  uint8_t red = 0;
  uint8_t green = 0;
  int32_t color = state.easter_egg ? state.easter_egg_fx : part_->model();
  if (state.easter_egg && color == RESONATOR_MODEL_FM_VOICE) {
    color = RESONATOR_MODEL_LAST;  // Breathing is reserved for the FM voice.
  }
  switch (color) {
    case RESONATOR_MODEL_MODAL:
      green = 15;
      break;
    case RESONATOR_MODEL_SYMPATHETIC_STRING:
      red = green = 15;
      break;
    case RESONATOR_MODEL_STRING:
      red = 15;
      break;
    case RESONATOR_MODEL_FM_VOICE:
      green = 15 - 15 * (t > 800 ? t - 800 : 800 - t) / 800;
      break;
    case RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED:
      red = phase ? 15 : 0;
      green = phase ? 0 : 15;
      break;
    case RESONATOR_MODEL_STRING_AND_REVERB:
      red = 15;
      green = phase ? 0 : 15;
      break;
    default:
      green = 15;
      red = phase ? 0 : 15;
      break;
  }
  bool dark = false;
  if (state.easter_egg) {
    dark = (ms & 127) < 48;
  } else {
    for (int32_t i = 0; i < state.exciter; ++i) {
      uint32_t start = 460 + 200 * i;
      dark = dark || (t >= start && t < start + 80);
    }
  }
  if (dark) {
    red = green = 0;
  }
  leds_.set(1, pwm_counter < red, pwm_counter < green);
}

void Ui::Poll() {
  // 1kHz.
  system_clock.Tick();
  switches_.Debounce();

  for (uint8_t i = 0; i < kNumSwitches; ++i) {
    if (switches_.just_pressed(i)) {
      press_time_[i] = system_clock.milliseconds();
    }
    if (switches_.pressed(i) && press_time_[i] != 0) {
      int32_t pressed_time = system_clock.milliseconds() - press_time_[i];
      if (pressed_time > kLongPressDuration) {
        queue_.AddEvent(CONTROL_SWITCH_HOLD, i, pressed_time);
        press_time_[i] = 0;
      } else if (pressed_time > kMediumPressDuration &&
                 mode_ == UI_MODE_NORMAL &&
                 switches_.pressed(1 - i)) {
        mode_ = UI_MODE_DISPLAY_FREQUENCY_LOCKING;
      }
    }
    if (switches_.released(i) && press_time_[i] != 0) {
      queue_.AddEvent(
          CONTROL_SWITCH,
          i,
          system_clock.milliseconds() - press_time_[i] + 1);
      press_time_[i] = 0;
    }
  }
  
  bool blink = (system_clock.milliseconds() & 127) > 64;
  bool slow_blink = (system_clock.milliseconds() & 255) > 128;
  switch (mode_) {
    case UI_MODE_NORMAL:
      {
        uint8_t pwm_counter = system_clock.milliseconds() & 15;
        uint8_t triangle = (system_clock.milliseconds() >> 5) & 31;
        triangle = triangle < 16 ? triangle : 31 - triangle;

        if (settings_->state().color_blind == 1) {
          uint8_t poly_counter = (system_clock.milliseconds() >> 7) % 12;
          uint8_t poly_brightness = (poly_counter >> 1) < part_->polyphony() &&
                (poly_counter & 1);
          uint8_t poly_red_brightness = part_->polyphony() >= 2
              ? 8 + 8 * poly_brightness
              : 0;
          uint8_t poly_green_brightness = part_->polyphony() <= 3
              ? 8 + 8 * poly_brightness
              : 0;
          if (part_->polyphony() == 1 || part_->polyphony() == 4) {
            poly_red_brightness >>= 3;
            poly_green_brightness >>= 2;
          }
          leds_.set(
              0,
              pwm_counter < poly_red_brightness,
              pwm_counter < poly_green_brightness);
        } else {
          leds_.set(0, part_->polyphony() >= 2, part_->polyphony() <= 2);
          // Fancy modes!
          if (part_->polyphony() == 3) {
            leds_.set(0, true, pwm_counter < triangle);
          }
        }
        RenderSoundLed(system_clock.milliseconds(), pwm_counter);
        ++strumming_flag_interval_;
        if (strumming_flag_counter_) {
          --strumming_flag_counter_;
          leds_.set(0, false, false);
        }
      }
      break;
    
    case UI_MODE_CALIBRATION_C1:
      leds_.set(0, blink, blink);
      leds_.set(1, false, false);
      break;

    case UI_MODE_CALIBRATION_C3:
      leds_.set(0, false, false);
      leds_.set(1, blink, blink);
      break;

    case UI_MODE_CALIBRATION_LOW:
      leds_.set(0, slow_blink, 0);
      leds_.set(1, slow_blink, 0);
      break;

    case UI_MODE_CALIBRATION_HIGH:
      leds_.set(0, false, slow_blink);
      leds_.set(1, false, slow_blink);
      break;

    case UI_MODE_DISPLAY_FREQUENCY_LOCKING:
      // Display the opposite of the current setting since we only toggle
      // on release.
      if (settings_->state().frequency_locked) {
        leds_.set(0, 0, blink);
        leds_.set(1, 0, blink);
      } else {
        leds_.set(0, blink, 0);
        leds_.set(1, blink, 0);
      }
      break;

    case UI_MODE_PANIC:
      leds_.set(0, blink, false);
      leds_.set(1, blink, false);
      break;
  }
  leds_.Write();
}

void Ui::FlushEvents() {
  queue_.Flush();
}

void Ui::IgnoreSwitchReleases() {
  press_time_[0] = press_time_[1] = 0;
}

void Ui::OnSwitchLongHeld(const Event& e) {
  if (switches_.pressed(1 - e.control_id)) {
    // Both switches held, starting with the right one: calibration.
    if (e.control_id == 1) {
      if (mode_ == UI_MODE_CALIBRATION_C1) {
        StartNormalizationCalibration();
      } else {
        StartCalibration();
      }
    } else {
      // Starting with the left one: used to open the options menu, now
      // just cancels.
      mode_ = UI_MODE_NORMAL;
    }
  } else if (e.control_id == 0) {
    StepPolyphony();
  } else {
    ToggleEasterEgg();
  }
  IgnoreSwitchReleases();
}

void Ui::OnSwitchReleased(const Event& e) {
  switch (mode_) {
    case UI_MODE_CALIBRATION_C1:
      if (e.control_id == 0) {
        CalibrateC1();
      }
      break;
    case UI_MODE_CALIBRATION_C3:
      if (e.control_id == 0) {
        CalibrateC3();
      }
      break;
    case UI_MODE_CALIBRATION_LOW:
      if (e.control_id == 0) {
        CalibrateLow();
      }
      break;
    case UI_MODE_CALIBRATION_HIGH:
      if (e.control_id == 0) {
        CalibrateHigh();
      }
      break;
    case UI_MODE_DISPLAY_FREQUENCY_LOCKING:
      if (switches_.pressed(1 - e.control_id)) {
        settings_->ToggleFrequencyLocking();
      }
      IgnoreSwitchReleases();
      mode_ = UI_MODE_NORMAL;
      break;
    default:
      // Plaits-style: left = previous sound, right = next sound.
      StepSound(e.control_id == 0 ? -1 : 1);
      break;
  }
}

void Ui::StartCalibration() {
  mode_ = UI_MODE_CALIBRATION_C1;
}

void Ui::CalibrateC1() {
  cv_scaler_->CalibrateC1();
  cv_scaler_->CalibrateOffsets();
  mode_ = UI_MODE_CALIBRATION_C3;
}

void Ui::CalibrateC3() {
  bool success = cv_scaler_->CalibrateC3();
  if (success) {
    settings_->Save();
    mode_ = UI_MODE_NORMAL;
  } else {
    mode_ = UI_MODE_PANIC;
  }
}

void Ui::StartNormalizationCalibration() {
  cv_scaler_->StartNormalizationCalibration();
  mode_ = UI_MODE_CALIBRATION_LOW;
}

void Ui::CalibrateLow() {
  cv_scaler_->CalibrateLow();
  mode_ = UI_MODE_CALIBRATION_HIGH;
}

void Ui::CalibrateHigh() {
  bool success = cv_scaler_->CalibrateHigh();
  if (success) {
    settings_->Save();
    mode_ = UI_MODE_NORMAL;
  } else {
    mode_ = UI_MODE_PANIC;
  }
}

void Ui::DoEvents() {
  while (queue_.available()) {
    Event e = queue_.PullEvent();
    if (e.control_type == CONTROL_SWITCH) {
      OnSwitchReleased(e);
    } else if (e.control_type == CONTROL_SWITCH_HOLD) {
      OnSwitchLongHeld(e);
    }
  }
  if (queue_.idle_time() > 800 && mode_ == UI_MODE_PANIC) {
    mode_ = UI_MODE_NORMAL;
  }
  if (queue_.idle_time() > 1000) {
    queue_.Touch();
  }
}

uint8_t Ui::HandleFactoryTestingRequest(uint8_t command) {
  uint8_t argument = command & 0x1f;
  command = command >> 5;
  uint8_t reply = 0;
  switch (command) {
    case FACTORY_TESTING_READ_POT:
    case FACTORY_TESTING_READ_CV:
      reply = cv_scaler_->adc_value(argument);
      break;
    
    case FACTORY_TESTING_READ_NORMALIZATION:
      reply = cv_scaler_->normalization(argument);
      break;      
    
    case FACTORY_TESTING_READ_GATE:
      reply = argument == 2
          ? cv_scaler_->gate_value()
          : switches_.pressed(argument);
      break;
      
    case FACTORY_TESTING_SET_BYPASS:
      part_->set_bypass(argument);
      break;
      
    case FACTORY_TESTING_CALIBRATE:
      {
        switch (argument) {
          case 0:
            StartCalibration();
            break;
          
          case 1:
            CalibrateC1();
            break;
          
          case 2:
            CalibrateC3();
            break;
          
          case 3:
            StartNormalizationCalibration();
            break;

          case 4:
            CalibrateLow();
            break;
          
          case 5:
            CalibrateHigh();
            queue_.Touch();
            break;
        }
      }
      break;
  }
  return reply;
}

}  // namespace rings
