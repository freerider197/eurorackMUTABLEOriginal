// Full Part engine, FM model: checks Hold on Strum. part_hold_test <hold 0|1> <mode 0-3> <polyphony>
#include "rings/dsp/part.h"
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace rings;
static uint16_t reverb_buffer[32768];
// Renders blocks with a given brightness; returns RMS of high-passed output
// (a crude "brightness meter": energy above the fundamental).
static float run(Part& part, PerformanceState& ps, Patch& p, int blocks, bool strum_first) {
  float in[24] = {0}, out[24], aux[24]; double s = 0, t = 0; float prev = 0;
  for (int b = 0; b < blocks; ++b) {
    ps.strum = strum_first && b == 0;
    part.Process(ps, p, in, out, aux, 24);
    for (int i = 0; i < 24; ++i) { float d = out[i] - prev; prev = out[i]; s += d * d; t += out[i] * out[i]; }
  }
  return sqrt(s / (t + 1e-9));
}
int main(int argc, char** argv) {
  int hold = atoi(argv[1]); int mode = atoi(argv[2]); int poly = atoi(argv[3]);
  Part part; part.Init(reverb_buffer);
  part.set_polyphony(poly); part.set_model(RESONATOR_MODEL_FM_VOICE);
  PerformanceState ps; memset(&ps, 0, sizeof(ps));
  ps.internal_exciter = true; ps.internal_strum = false; ps.internal_note = true;
  ps.mode = (PerformanceMode)mode; ps.tonic = 48.0f; ps.note = 0.0f;
  ps.strum_hold_option = hold;
  Patch p = { 0.5f, 0.3f, 0.95f, 0.5f };  // damping .95 = near drone
  run(part, ps, p, 400, true);              // strum with brightness 0.3
  float a = run(part, ps, p, 200, false);
  p.brightness = 0.9f;                      // turn brightness up, no strum
  run(part, ps, p, 200, false);
  float b = run(part, ps, p, 200, false);
  float c = (run(part, ps, p, 400, true), run(part, ps, p, 200, false));  // strum
  printf("mode=%d poly=%d hold=%d  brightness meter: before %.3f | knob moved, no strum %.3f | after strum %.3f\n", mode, poly, hold, a, b, c);
}
