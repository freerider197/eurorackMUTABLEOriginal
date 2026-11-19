// Tomline: bass tom / low tom voice for the Mutable Instruments Streams
// hardware. Alternative firmware, built on the Streams drivers and
// calibration code (Copyright 2014 Emilie Gillet, MIT license).
//
// Patch: trigger into EXCITE 1, V/oct into EXCITE 2, sound from OUT 1.
// Set channel 1's response trimmer fully clockwise (LIN) and LEVEL MOD 1
// fully counter-clockwise. IN 1 is not used.

#include "stmlib/system/system_clock.h"

#include "streams/cv_scaler.h"
#include "streams/drivers/adc.h"
#include "streams/drivers/dac.h"
#include "streams/drivers/pwm.h"
#include "streams/drivers/system.h"
#include "streams/gain.h"

#include "tomline/tables.h"
#include "tomline/ui.h"
#include "tomline/voice.h"

using namespace stmlib;
using namespace streams;
using namespace tomline;

Adc adc;
CvScaler cv_scaler;
Dac dac;
Pwm pwm;
System sys;
Ui ui;
Voice voice;

bool gate = false;

// DIAGNOSTIC BUILD (test tone): no voice. Channel 1 plays a steady 220 Hz
// tone, channel 2 a steady 330 Hz tone, filters open. Increments at the
// 31,088 Hz firmware rate: f / 31088.08 * 2^32.
uint32_t tone_phase[2] = { 0, 0 };
const uint32_t kToneIncrement[2] = { 30394693, 45592040 };

extern "C" {

void HardFault_Handler() { while (1); }
void MemManage_Handler() { while (1); }
void BusFault_Handler() { while (1); }
void UsageFault_Handler() { while (1); }
void NMI_Handler() { }
void SVC_Handler() { }
void DebugMon_Handler() { }
void PendSV_Handler() { }

void SysTick_Handler() {
  ui.Poll();  // 4 kHz
}

}

// Called by the ADC DMA interrupt after each scan of the 6 inputs (31.088 kHz).
void Process(uint16_t* cv) {
  // EXCITE 1: trigger, with the stock Schmitt trigger thresholds (about 1.7 V
  // on, 0.85 V off).
  int16_t excite = cv_scaler.excite_sample(0);
  bool trigger = false;
  if (!gate && excite > kSchmittTriggerThreshold) {
    gate = true;
    trigger = true;
  } else if (gate && excite < (kSchmittTriggerThreshold >> 1)) {
    gate = false;
  }

  int32_t pitch = ui.Pitch(cv_scaler.excite_sample(1));

  (void)pitch;
  uint16_t gain = 0;
  uint16_t tone_gain[2];
  for (uint8_t ch = 0; ch < 2; ++ch) {
    tone_phase[ch] += kToneIncrement[ch];
    int32_t s = lut_sine[tone_phase[ch] >> 22];
    tone_gain[ch] = static_cast<uint16_t>(16384 + (s * 12000 >> 15));
  }
  gain = tone_gain[0];

  if (ui.calibrating()) {
    // Stock calibration: VCAs closed, filters open.
    dac.Write(0, cv_scaler.ScaleGain(0, 0));
    pwm.Write(0, 0);
    dac.Write(1, cv_scaler.ScaleGain(1, 0));
    pwm.Write(1, 0);
  } else {
    dac.Write(0, cv_scaler.ScaleGain(0, tone_gain[0]));
    pwm.Write(0, 0);  // filter open
    dac.Write(1, cv_scaler.ScaleGain(1, tone_gain[1]));
    pwm.Write(1, 0);
  }
  ui.OnSample(trigger, gain, cv_scaler.raw_gain_sample(0), voice.lfo_phase());
}

void Init() {
  sys.Init(true);
  system_clock.Init();
  adc.Init(false, &Process);
  cv_scaler.Init(&adc);
  dac.Init();
  pwm.Init();
  voice.Init();
  ui.Init(&adc, &cv_scaler);
  sys.StartTimers();
  adc.Start();
}

int main(void) {
  Init();
  while (1) {
    ui.DoEvents();
  }
}
