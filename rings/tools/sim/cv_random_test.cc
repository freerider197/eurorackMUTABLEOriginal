// Real CvScaler::Read() on stubbed hardware: which parameters get a random
// value per STRUM trigger when their CV jack is empty.
#include "rings/cv_scaler.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"
#include <cstdio>
#include <cstring>
#include <cmath>
using namespace rings;
namespace rings { bool g_probe_state, g_strum_patched, g_strum_edge; }
namespace stmlib { int g_saves; }
Settings settings; CvScaler cv; Patch patch; PerformanceState ps;
static void set(int ch, float v) { cv.adc_.values_[ch] = (uint16_t)(v * 65535.0f); }
static void run(int blocks, bool strum = false) {
  for (int i = 0; i < blocks; ++i) { g_strum_edge = strum && i == 0; cv.Read(&patch, &ps, &settings); }
}
static const char* names[] = { "Structure", "Brightness", "Damping", "Position" };
static float* params[] = { &patch.structure, &patch.brightness, &patch.damping, &patch.position };
static void scenario(const char* title, float attenuverter, bool cable_in_cv) {
  for (int c = ADC_CHANNEL_CV_STRUCTURE; c <= ADC_CHANNEL_CV_POSITION; ++c) set(c, cable_in_cv ? 0.3f : 0.5f);
  for (int c = ADC_CHANNEL_ATTENUVERTER_STRUCTURE; c <= ADC_CHANNEL_ATTENUVERTER_POSITION; ++c) set(c, attenuverter);
  run(6000);  // settle filters and the 1s empty-jack detection
  float lo[4], hi[4];
  for (int k = 0; k < 4; ++k) lo[k] = hi[k] = *params[k];
  for (int t = 0; t < 40; ++t) {
    run(200, true);  // a STRUM trigger every 0.1s
    for (int k = 0; k < 4; ++k) { lo[k] = fminf(lo[k], *params[k]); hi[k] = fmaxf(hi[k], *params[k]); }
  }
  printf("%s\n", title);
  for (int k = 0; k < 4; ++k)
    printf("  %-10s %.3f .. %.3f  %s\n", names[k], lo[k], hi[k], hi[k] - lo[k] > 0.02f ? "RANDOM" : "steady");
}
int main() {
  CalibrationData* cal = settings.mutable_calibration_data();
  settings.Init();
  for (int c = 0; c < ADC_CHANNEL_NUM_OFFSETS; ++c) cal->offset[c] = 0.5f;
  cv.Init(cal);
  memset(cv.adc_.values_, 0, sizeof(cv.adc_.values_));
  for (int c = ADC_CHANNEL_POT_STRUCTURE; c <= ADC_CHANNEL_POT_POSITION; ++c) set(c, 0.3f);  // knobs at 0.3
  set(ADC_CHANNEL_CV_FREQUENCY, 0.5f); set(ADC_CHANNEL_CV_V_OCT, 0.5f); set(ADC_CHANNEL_ATTENUVERTER_FREQUENCY, 0.5f);
  g_strum_patched = true;
  scenario("Cable in STRUM, CV jacks empty, attenuverters fully clockwise:", 1.0f, false);
  scenario("Cable in STRUM, CV jacks empty, attenuverters at noon:", 0.5f, false);
  scenario("Cable in STRUM, cables in the CV jacks (steady voltage), attenuverters fully clockwise:", 1.0f, true);
  g_strum_patched = false;
  scenario("No cable in STRUM, CV jacks empty, attenuverters fully clockwise:", 1.0f, false);
}
