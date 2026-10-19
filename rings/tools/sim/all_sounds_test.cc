#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace rings;
static uint16_t reverb_buffer[32768];
int main() {
  const char* exc[] = { "Rings", "Bow", "Blow", "Strike" };
  int bad = 0;
  for (int poly = 1; poly <= 4; ++poly) for (int m = 0; m < 6; ++m) for (int e = 0; e < 4; ++e) {
    static Part part; part.Init(reverb_buffer); part.set_polyphony(poly); part.set_model((ResonatorModel)m);
    PerformanceState ps; memset(&ps, 0, sizeof(ps));
    ps.internal_exciter = true; ps.internal_note = true; ps.tonic = 48; ps.mode = e ? MODE_MINI_ELEMENTS_STEREO : MODE_RINGS_STEREO;
    ps.waveform_exciter = e ? e - 1 : 0;
    Patch p = { 0.4f, 0.6f, 0.6f, 0.3f };
    float in[24] = {0}, out[24], aux[24]; double s = 0; bool nan = false;
    for (int b = 0; b < 4000; ++b) {   // 2s, strum every 0.25s, knobs moving
      ps.strum = ps.strum_gate = (b % 500) < 2;
      p.structure = 0.5f + 0.49f * sinf(b * 0.003f); p.position = 0.5f + 0.49f * sinf(b * 0.0021f);
      part.Process(ps, p, in, out, aux, 24);
      for (int i = 0; i < 24; ++i) { if (!std::isfinite(out[i]) || !std::isfinite(aux[i])) nan = true; s += out[i] * out[i]; }
    }
    double rms = sqrt(s / (4000 * 24));
    if (nan || rms < 1e-4) { ++bad; printf("PROBLEM poly %d model %d %s: nan=%d rms=%.5f\n", poly, m, exc[e], nan, rms); }
  }
  static StringSynthPart ss; ss.Init(reverb_buffer);
  for (int fx = 0; fx < 6; ++fx) for (int poly = 1; poly <= 4; ++poly) {
    ss.set_polyphony(poly); ss.set_fx((FxType)fx);
    PerformanceState ps; memset(&ps, 0, sizeof(ps)); ps.tonic = 48; ps.mode = MODE_EASTER_EGG; ps.internal_exciter = true; ps.internal_note = true;
    Patch p = { 0.4f, 0.6f, 0.6f, 0.5f }; float in[24] = {0}, out[24], aux[24]; bool nan = false; double s = 0;
    for (int b = 0; b < 2000; ++b) { ps.strum = (b % 400) == 0; ss.Process(ps, p, in, out, aux, 24); for (int i = 0; i < 24; ++i) { if (!std::isfinite(out[i])) nan = true; s += out[i]*out[i]; } }
    if (nan || sqrt(s / 48000) < 1e-4) { ++bad; printf("PROBLEM easter egg fx %d poly %d\n", fx, poly); }
  }
  printf("%s: 96 resonator/exciter/polyphony combos + 24 easter egg fx/polyphony combos\n", bad ? "FAILURES" : "all OK");
}
