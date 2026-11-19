// Host test for Tomline: runs the firmware's own voice.cc and parameters.cc
// on a computer, feeds the VCA/filter words into a model of the Streams
// analog stage, and writes WAV files plus a few checks.
//
// Build and run from the eurorack root:
//   g++ -O2 -I. -o build/tomline_render tomline/test/render.cc
//       tomline/voice.cc tomline/parameters.cc; ./build/tomline_render
//
// Analog model (values from the Streams v05 schematic and streams/gain.h):
//   DAC -> 2 x RC 1k/10nF, 10k/1nF (about 16 kHz each)
//   VCA, response trimmer at LIN: gain = word / 32767, input +3.33 V (IN 1
//     unpatched), so OUT = 3.33 V * word / 32767
//   Filter: two 1-pole low-pass stages, cutoff from the PWM word:
//     firmware smoothing (1/16 per sample), RC 1.2k/680nF (195 Hz), then
//     cutoff = 48 kHz * 2^(-16.6 * (1 - frequency / 65535)) (2164 at
//     -33 mV/dB, 100k / 33 pF integrators). Nominal values, not measured.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "tomline/parameters.h"
#include "tomline/tables.h"
#include "tomline/voice.h"

using namespace tomline;

const double kFirmwareRate = kSampleRate;
const double kHostRate = 96000.0;
const int32_t kCountsPerVolt = 3972;
const int32_t kVoctK = 12 * 65536 * 256 / kCountsPerVolt;

struct OnePole {
  double y = 0.0;
  double Process(double x, double fc, double sr) {
    double k = 1.0 - exp(-2.0 * M_PI * fc / sr);
    y += k * (x - y);
    return y;
  }
};

struct Step {
  int note;  // semitones above C1, -1 = rest
};

struct Patch {
  uint8_t engine;
  uint8_t snap;
  uint16_t main_layer[4];    // decay, tone, tune, sweep
  uint16_t second_layer[4];  // noise, +1 oct, sweep time, glide
};

struct Result {
  std::vector<float> raw;  // OUT 1 volts at kHostRate
  double min_v, max_v, rest_v;
};

Result Render(const Patch& patch, const std::vector<Step>& steps, double bpm,
              int repeats, bool external) {
  VoiceParameters p;
  ComputeVoiceParameters(patch.engine, patch.snap, patch.main_layer,
                         patch.second_layer, &p);
  Voice voice;
  voice.Init();
  Result r;
  r.min_v = 1e9;
  r.max_v = -1e9;
  double samples_per_step = kFirmwareRate * 60.0 / bpm / 4.0;
  int64_t total = static_cast<int64_t>(samples_per_step * steps.size() * repeats);
  total += static_cast<int64_t>(kFirmwareRate);  // 1 s tail
  OnePole dac1, dac2, f1, f2, pwm_rc;
  int32_t pwm_filtered = 0;
  double tick = 0.0;
  uint16_t gain = 0, frequency = 0;
  int32_t pitch = 0, counts = 0;
  int64_t n = 0;
  int step_index = -1;
  double next_step = 0.0;
  bool gate = false;
  while (n < total) {
    // Firmware sample.
    bool trigger = false;
    if (n >= next_step && step_index + 1 < static_cast<int>(steps.size() * repeats)) {
      ++step_index;
      next_step += samples_per_step;
      const Step& s = steps[step_index % steps.size()];
      if (s.note >= 0) {
        counts = s.note * kCountsPerVolt / 12;
        trigger = true;
        gate = true;
      }
    }
    if (gate && n > next_step - samples_per_step / 2) gate = false;
    int32_t semitones = counts * kVoctK >> 8;
    pitch += (semitones - pitch) >> 5;
    voice.Process(p, trigger, pitch, external, &gain, &frequency);
    pwm_filtered += ((65535 - frequency) - pwm_filtered) >> 4;
    int32_t pwm_word = pwm_filtered >> 6;  // 10-bit PWM
    ++n;
    // Analog side at the host rate until the next firmware sample.
    tick += kHostRate / kFirmwareRate;
    while (tick >= 1.0) {
      tick -= 1.0;
      double dac = gain / 32767.0;
      dac = dac2.Process(dac1.Process(dac, 15915.0, kHostRate), 15915.0, kHostRate);
      double vin = external ? 5.0 * sin(2 * M_PI * 65.4 * n / kFirmwareRate) : 3.33;
      double v = vin * dac;
      double pwm_v = pwm_rc.Process(3.3 * pwm_word / 1023.0, 195.0, kHostRate);
      double fc = 48000.0 * pow(2.0, -16.6 * pwm_v / 3.3);
      if (fc > 0.45 * kHostRate) fc = 0.45 * kHostRate;
      v = f2.Process(f1.Process(v, fc, kHostRate), fc, kHostRate);
      r.raw.push_back(static_cast<float>(v));
      if (v < r.min_v) r.min_v = v;
      if (v > r.max_v) r.max_v = v;
    }
  }
  r.rest_v = r.raw.back();
  return r;
}

void WriteWav(const char* path, const std::vector<float>& volts, bool ac) {
  // 48 kHz, 16-bit. AC-coupled version for listening: 3 Hz high-pass, like
  // an audio interface input. 10 V = full scale.
  std::vector<int16_t> pcm;
  double x1 = 0, y1 = 0, r = 1.0 - 2 * M_PI * 3.0 / kHostRate;
  for (size_t i = 0; i + 1 < volts.size(); i += 2) {
    double v = 0;
    for (int k = 0; k < 2; ++k) {
      double x = volts[i + k];
      double y = ac ? x - x1 + r * y1 : x;
      x1 = x; y1 = y;
      v += y * 0.5;
    }
    double s = v / 10.0;
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    pcm.push_back(static_cast<int16_t>(s * 32767));
  }
  FILE* f = fopen(path, "wb");
  uint32_t data = pcm.size() * 2, riff = 36 + data, sr = 48000, br = 96000;
  uint16_t one = 1, two = 2, bits = 16;
  uint32_t sixteen = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&sixteen, 4, 1, f); fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f);
  fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&two, 2, 1, f);
  fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  fwrite(pcm.data(), 2, pcm.size(), f);
  fclose(f);
}

double MeasurePitch(const Patch& base, int note) {
  Patch patch = base;
  patch.main_layer[0] = 65535;  // longest decay
  patch.main_layer[1] = 65535;  // filter open
  patch.main_layer[3] = 0;      // no sweep
  patch.second_layer[0] = 0;    // no noise
  patch.second_layer[1] = 0;    // no octave
  std::vector<Step> steps(1, Step{note});
  Result r = Render(patch, steps, 30.0, 1, false);
  // Zero crossings of the AC part between 0.2 s and 1.2 s.
  size_t a = static_cast<size_t>(0.2 * kHostRate), b = static_cast<size_t>(1.2 * kHostRate);
  double mean = 0;
  for (size_t i = a; i < b; ++i) mean += r.raw[i];
  mean /= (b - a);
  // Remove the slowly decaying lift with a moving average over one period.
  int crossings = 0;
  double first = -1, last = -1;
  double prev = 0;
  const size_t w = 2000;
  for (size_t i = a + w; i < b; ++i) {
    double local = 0;
    for (size_t k = i - w; k < i; k += 50) local += r.raw[k];
    local /= (w / 50);
    double x = r.raw[i] - local;
    if (prev < 0 && x >= 0) {
      double t = (i - 1 + (-prev) / (x - prev)) / kHostRate;
      if (first < 0) first = t;
      last = t;
      ++crossings;
    }
    prev = x;
  }
  (void)mean;
  return crossings > 1 ? (crossings - 1) / (last - first) : 0.0;
}

// Glide check: C2 then C3 with glide at 50 % (time constant 129 ms). After
// 1.5 s the second note must sit on C3.
double GlideErrorCents() {
  Patch patch = { ENGINE_BT, 0, { 65535, 65535, 32768, 0 }, { 0, 0, 0, 32768 } };
  std::vector<Step> steps = { {12}, {24} };
  Result r = Render(patch, steps, 10.0, 1, false);  // 1.5 s per step
  // Zero crossings between 1.5 s and 2.5 s into the second note.
  double start = 1.5 + 1.5, end = 1.5 + 2.5;
  size_t a = static_cast<size_t>(start * kHostRate), b = static_cast<size_t>(end * kHostRate);
  int crossings = 0;
  double first = -1, last = -1, prev = 0;
  const size_t w = 2000;
  for (size_t i = a + w; i < b; ++i) {
    double local = 0;
    for (size_t k = i - w; k < i; k += 50) local += r.raw[k];
    local /= (w / 50);
    double x = r.raw[i] - local;
    if (prev < 0 && x >= 0) {
      double t = (i - 1 + (-prev) / (x - prev)) / kHostRate;
      if (first < 0) first = t;
      last = t;
      ++crossings;
    }
    prev = x;
  }
  double measured = (crossings - 1) / (last - first);
  return 1200.0 * log2(measured / (32.70319566 * 4.0));
}

int main() {
  int failures = 0;
  Patch bt = { ENGINE_BT, 1, { 29491, 40632, 32768, 19661 },
               { 9830, 16384, 22938, 0 } };
  Patch lt = bt;
  lt.engine = ENGINE_LT;
  Patch bt_oct = bt;
  bt_oct.second_layer[1] = 39321;  // +1 octave at 60 %

  // "Roller" pattern from the browser page, notes above C1.
  std::vector<Step> roller = {
    {12}, {-1}, {12}, {24}, {-1}, {15}, {12}, {10},
    {12}, {-1}, {12}, {19}, {17}, {15}, {-1}, {10}};

  struct { const char* name; Patch patch; } cases[] = {
    { "build/tomline_bt.wav", bt },
    { "build/tomline_lt.wav", lt },
    { "build/tomline_bt_oct.wav", bt_oct },
  };
  for (auto& c : cases) {
    Result r = Render(c.patch, roller, 122.0, 4, false);
    WriteWav(c.name, r.raw, true);
    printf("%-28s OUT 1 min %.3f V  max %.2f V  after the tail %.4f V\n",
           c.name, r.min_v, r.max_v, r.rest_v);
  }
  Result ext = Render(bt, roller, 122.0, 1, true);
  printf("%-28s OUT 1 min %.2f V  max %.2f V (external VCO into IN 1)\n",
         "external mode", ext.min_v, ext.max_v);

  printf("\nPitch at the default V/oct scale (0 V = C1), BT, tune centered:\n");
  int notes[] = { 0, 12, 24, 36 };
  for (int note : notes) {
    double expected = 32.70319566 * pow(2.0, note / 12.0);
    double measured = MeasurePitch(bt, note);
    printf("  %d V  expected %7.2f Hz  measured %7.2f Hz  error %+.1f cents\n",
           note / 12, expected, measured, 1200.0 * log2(measured / expected));
  }

  double glide = GlideErrorCents();
  bool glide_ok = fabs(glide) < 0.5;
  printf("\nGlide C2 -> C3, 1.5 s later: %+.2f cents  %s\n", glide,
         glide_ok ? "OK" : "FAIL (glide stops short of the note)");
  failures += !glide_ok;

  return failures ? 1 : 0;
}
