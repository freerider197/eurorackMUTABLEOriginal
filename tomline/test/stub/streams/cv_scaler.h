// Test stand-in for streams/cv_scaler.h: counts calibration calls.
#ifndef TOMLINE_TEST_STUB_CV_SCALER_H_
#define TOMLINE_TEST_STUB_CV_SCALER_H_

#include "stmlib/stmlib.h"
#include "streams/drivers/adc.h"

namespace streams {

extern bool sim_can_calibrate;
extern int sim_offset_captures, sim_calibration_saves, sim_vca_offset_changes;

class CvScaler {
 public:
  bool can_calibrate() const { return sim_can_calibrate; }
  void CaptureAdcOffsets() { ++sim_offset_captures; }
  void SaveCalibrationData() { ++sim_calibration_saves; }
  void set_dac_offset(uint8_t, int32_t) { ++sim_vca_offset_changes; }
  uint16_t raw_gain_sample(uint8_t) const { return 60000; }
};

}  // namespace streams

#endif  // TOMLINE_TEST_STUB_CV_SCALER_H_
