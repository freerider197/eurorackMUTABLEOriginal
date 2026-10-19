#ifndef RINGS_CV_SCALER_H_
#define RINGS_CV_SCALER_H_
#include "rings/settings.h"
namespace rings {
class CvScaler {
 public:
  void CalibrateC1() { } void CalibrateOffsets() { } bool CalibrateC3() { return true; }
  void StartNormalizationCalibration() { } void CalibrateLow() { } bool CalibrateHigh() { return true; }
  uint8_t adc_value(int) { return 0; } uint8_t gate_value() { return 0; } uint8_t normalization(int) { return 0; }
};
}
#endif
