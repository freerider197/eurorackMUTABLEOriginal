// Dumps the firmware's VCA and filter words for a fixed pattern, so
// tomline/test/compare_browser.js can check the browser port against them.
//
//   g++ -O2 -I. -o build/tomline_words tomline/test/words.cc tomline/voice.cc tomline/parameters.cc
//   ./build/tomline_words
//   node tomline/test/compare_browser.js

#include <cstdio>
#include <vector>

#include "tomline/parameters.h"
#include "tomline/tables.h"
#include "tomline/voice.h"

using namespace tomline;

int main() {
  const int roller[16] = { 12, -1, 12, 24, -1, 15, 12, 10, 12, -1, 12, 19, 17, 15, -1, 10 };
  const int32_t kCountsPerVolt = 3972;
  const int32_t kVoctK = 12 * 65536 * 256 / kCountsPerVolt;
  struct Case { const char* path; uint8_t engine; uint8_t snap; uint16_t main[4]; uint16_t second[4];
                uint16_t lfo[4]; uint8_t lfo_shape; bool lfo_free; uint16_t duck[2]; };
  Case cases[] = {
    { "build/tomline_words_bt.bin", ENGINE_BT, 1, { 29491, 40632, 32768, 19661 }, { 0, 16384, 0, 0 } },
    { "build/tomline_words_lt.bin", ENGINE_LT, 1, { 29491, 40632, 32768, 19661 }, { 0, 16384, 0, 0 } },
    { "build/tomline_words_glide.bin", ENGINE_BT, 0, { 40000, 20000, 30000, 40000 }, { 30000, 50000, 40000, 30000 } },
    { "build/tomline_words_extreme.bin", ENGINE_LT, 3, { 10000, 60000, 50000, 65535 }, { 65535, 65535, 65535, 0 } },
    { "build/tomline_words_lfo_tri.bin", ENGINE_BT, 1, { 29491, 40632, 32768, 19661 }, { 0, 16384, 0, 30000 },
      { 50000, 40000, 0, 0 }, LFO_TRIANGLE },
    { "build/tomline_words_lfo_ramp.bin", ENGINE_LT, 2, { 40000, 30000, 20000, 0 }, { 0, 30000, 40000, 0 },
      { 20000, 65535, 0, 0 }, LFO_RAMP_UP },
    { "build/tomline_words_lfo_tri_free.bin", ENGINE_BT, 1, { 29491, 40632, 32768, 19661 }, { 0, 16384, 0, 30000 },
      { 50000, 40000, 0, 0 }, LFO_TRIANGLE, true },
    { "build/tomline_words_lfo_ramp_free.bin", ENGINE_LT, 2, { 40000, 30000, 20000, 0 }, { 0, 30000, 40000, 0 },
      { 20000, 65535, 0, 0 }, LFO_RAMP_UP, true },
    { "build/tomline_words_wave_tri.bin", ENGINE_BT, 1, { 29491, 60000, 32768, 19661 }, { 0, 16384, 12000, 0 } },
    { "build/tomline_words_wave_fold.bin", ENGINE_LT, 2, { 45000, 60000, 32768, 0 }, { 0, 20000, 50000, 0 } },
    { "build/tomline_words_wave_square.bin", ENGINE_BT, 0, { 50000, 50000, 20000, 40000 }, { 0, 0, 65535, 20000 } },
    { "build/tomline_words_crush_rate.bin", ENGINE_BT, 1, { 40000, 60000, 32768, 19661 }, { 20000, 16384, 0, 0 } },
    { "build/tomline_words_crush_low.bin", ENGINE_LT, 2, { 50000, 65535, 32768, 30000 }, { 55000, 16384, 30000, 0 } },
    { "build/tomline_words_sweep.bin", ENGINE_BT, 1, { 29491, 60000, 32768, 19661 }, { 0, 16384, 0, 20000 }, { 50000, 0, 0, 40000 }, LFO_TRIANGLE, false },
    { "build/tomline_words_duck_full.bin", ENGINE_BT, 1, { 29491, 40632, 32768, 19661 }, { 0, 16384, 0, 0 }, { 32768, 0, 0, 0 }, LFO_TRIANGLE, false, { 65535, 32768 } },
    { "build/tomline_words_duck_fast.bin", ENGINE_LT, 2, { 50000, 60000, 32768, 30000 }, { 0, 16384, 20000, 0 }, { 32768, 0, 0, 0 }, LFO_TRIANGLE, false, { 40000, 0 } },
    { "build/tomline_words_duck_slow.bin", ENGINE_BT, 0, { 40000, 50000, 20000, 0 }, { 0, 0, 0, 0 }, { 32768, 0, 0, 0 }, LFO_TRIANGLE, false, { 52000, 65535 } },
  };
  for (const Case& c : cases) {
    VoiceParameters p;
    ComputeVoiceParameters(c.engine, c.snap, c.main, c.second, c.lfo, c.lfo_shape, &p, c.lfo_free,
                           c.duck);
    Voice voice;
    voice.Init();
    double samples_per_step = kSampleRate * 60.0 / 122.0 / 4.0;
    double next_step = 0.0;
    int step = -1;
    int32_t counts = 0, pitch = 0;
    std::vector<uint16_t> out;
    // IN 2: an integer kick on every beat (a decaying triangle, about 52 Hz),
    // the same in compare_browser.js.
    int32_t kick_amp = 0, kick_phase = 0;
    for (int64_t n = 0; n < static_cast<int64_t>(samples_per_step * 64); ++n) {
      bool trigger = false;
      if (n >= next_step) {
        ++step;
        next_step += samples_per_step;
        int note = roller[step % 16];
        if (note >= 0) {
          counts = note * kCountsPerVolt / 12;
          trigger = true;
        }
      }
      int32_t semitones = counts * kVoctK >> 8;
      pitch += (semitones - pitch) >> 5;
      uint16_t gain, frequency;
      if (n % 15288 == 0) { kick_amp = 24000; kick_phase = 0; }
      kick_phase = (kick_phase + 110) & 0xffff;
      int32_t tri = kick_phase < 32768 ? kick_phase - 16384 : 49152 - kick_phase;
      int32_t kick = tri * kick_amp >> 14;
      kick_amp -= kick_amp >> 13;
      voice.Process(p, trigger, pitch, kick, &gain, &frequency);
      out.push_back(gain);
      out.push_back(frequency);
    }
    FILE* f = fopen(c.path, "wb");
    fwrite(out.data(), 2, out.size(), f);
    fclose(f);
    printf("%s: %zu samples\n", c.path, out.size() / 2);
  }
  return 0;
}
