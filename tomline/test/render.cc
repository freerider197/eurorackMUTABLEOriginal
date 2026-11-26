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

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

#include "tomline/parameters.h"
#include "tomline/pickup.h"
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
  int note;  // V/oct in semitones (0 = 0 V), -1 = rest
};

struct Patch {
  uint8_t engine;
  uint8_t snap;
  uint16_t main_layer[4];    // decay, tone, tune, distortion
  uint16_t second_layer[4];  // crush, unused, wave, glide
  uint16_t lfo_layer[4];     // LFO speed, LFO depth, unused, unused
  uint8_t lfo_shape;         // LFO_TRIANGLE / LFO_RAMP_UP
  bool lfo_free;             // false = LFO restarts on each trigger
  uint16_t duck_layer[2] = { 0, 32768 };  // duck depth (off), release (noon)
};

// IN 2 (the sidechain) in ADC counts for firmware sample n; NULL = empty.
int32_t (*g_sidechain)(int64_t n) = NULL;

struct Result {
  std::vector<float> raw;  // OUT 1 volts at kHostRate
  double min_v, max_v, rest_v;
};

// Turns knobs while the patch plays: called before each firmware sample,
// returns true when it changed the patch.
typedef bool (*Control)(int64_t n, Patch* patch);

Result Render(const Patch& start, const std::vector<Step>& steps, double bpm,
              int repeats, Control control = NULL) {
  Patch patch = start;
  VoiceParameters p;
  ComputeVoiceParameters(patch.engine, patch.snap, patch.main_layer,
                         patch.second_layer, patch.lfo_layer, patch.lfo_shape, &p,
                         patch.lfo_free, patch.duck_layer);
  Voice voice;
  voice.Init();
  // Skip the power-up (1 s at 0 V, then the lift rises over about 1 s), with
  // the LFO held so it still starts at its lowest point.
  {
    VoiceParameters warm = p;
    warm.lfo_increment = 0;
    uint16_t g, f;
    for (int i = 0; i < 3 * kFirmwareRate; ++i) voice.Process(warm, false, 0, 0, &g, &f);
  }
  Result r;
  r.min_v = 1e9;
  r.max_v = -1e9;
  double samples_per_step = kFirmwareRate * 60.0 / bpm / 4.0;
  int64_t total = static_cast<int64_t>(samples_per_step * steps.size() * repeats);
  total += static_cast<int64_t>(kFirmwareRate);  // 1 s tail
  OnePole dac1, dac2, f1, f2, pwm_rc;
  dac1.y = dac2.y = 1.0;  // settled at the lift: word 32767
  f1.y = f2.y = 3.33;     // OUT 1 at rest
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
    if (control && control(n, &patch)) {
      ComputeVoiceParameters(patch.engine, patch.snap, patch.main_layer,
                             patch.second_layer, patch.lfo_layer, patch.lfo_shape, &p,
                             patch.lfo_free, patch.duck_layer);
    }
    int32_t semitones = counts * kVoctK >> 8;
    pitch += (semitones - pitch) >> 5;
    voice.Process(p, trigger, pitch, g_sidechain ? g_sidechain(n) : 0,
                  &gain, &frequency);
    pwm_filtered += ((65535 - frequency) - pwm_filtered) >> 4;
    int32_t pwm_word = pwm_filtered >> 6;  // 10-bit PWM
    ++n;
    // Analog side at the host rate until the next firmware sample.
    tick += kHostRate / kFirmwareRate;
    while (tick >= 1.0) {
      tick -= 1.0;
      double dac = gain / 32767.0;
      dac = dac2.Process(dac1.Process(dac, 15915.0, kHostRate), 15915.0, kHostRate);
      double v = 3.33 * dac;  // IN 1 unpatched: the VCA scales +3.33 V
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
  double x1 = ac && !volts.empty() ? volts[0] : 0, y1 = 0;
  double r = 1.0 - 2 * M_PI * 3.0 / kHostRate;
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
  patch.main_layer[3] = 0;      // no distortion
  patch.second_layer[0] = 0;    // no crush
  std::vector<Step> steps(1, Step{note});
  Result r = Render(patch, steps, 30.0, 1);
  // Zero crossings of the AC part between 0.2 s and 1.2 s.
  size_t a = static_cast<size_t>(0.2 * kHostRate), b = static_cast<size_t>(1.2 * kHostRate);
  double mean = 0;
  for (size_t i = a; i < b; ++i) mean += r.raw[i];
  mean /= (b - a);
  // Remove the lift with a moving average over one period.
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

// Glide check: C2 then C3 (0 V, 1 V, tune at noon) with glide at 50 % (time
// constant 129 ms). After 1.5 s the second note must sit on C3.
double GlideErrorCents() {
  Patch patch = { ENGINE_BT, 0, { 65535, 65535, 32768, 0 }, { 0, 0, 0, 32768 } };
  std::vector<Step> steps = { {0}, {12} };
  Result r = Render(patch, steps, 10.0, 1);  // 1.5 s per step
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

// Overflow check: the output mix (tone and snap) stays under 50000, but
// scaling any mix must clip at the top, never wrap to the bottom.
bool MixScalingOk() {
  // With MOD 2 at full (gain word 10923) the soft clipper gets about
  // 218000: it must stay at full scale too.
  return Voice::ScaleMix(82000) == 32767 && Voice::ScaleMix(70000) == 32767 &&
         Voice::ScaleMix(-82000) == -32767 && Voice::ScaleMix(1000) == 950 &&
         Voice::SoftClip(82000 * 10923 >> 12) == 32767 &&
         Voice::SoftClip(-82000 * 10923 >> 12) == -32767;
}

// Tune range: SHAPE 2 fully left = -12 st (C1 at 0 V), noon = C2, fully
// right = +12 st (C3). Returns the worst error in semitones.
double TuneRangeError() {
  const uint16_t knob[3] = { 0, 32768, 65535 };
  const double expected[3] = { 0.0, 12.0, 24.0 };  // semitones above C1
  double worst = 0;
  for (int i = 0; i < 3; ++i) {
    Patch patch = { ENGINE_BT, 2, { 9146, 40632, knob[i], 19661 },
                    { 0, 16384, 0, 0 } };
    VoiceParameters p;
    ComputeVoiceParameters(patch.engine, patch.snap, patch.main_layer,
                           patch.second_layer, patch.lfo_layer, patch.lfo_shape, &p);
    worst = fmax(worst, fabs(p.tune / 65536.0 - expected[i]));
  }
  return worst;
}

// Pitch LFO, depth fully up (+/-12 st), speed fully down (0.05 Hz, one cycle
// in 20 s), rising by 1.2 st/s (ramp up) or 2.4 st/s (triangle).
// Restart on trigger: the note starts at the middle of the wave, so 0.2 to
// 1.2 s in, the pitch is just above C2. Free running: the LFO starts at its
// lowest point when the module starts, about 12 st below C2. Returns the error
// of the measured average pitch in semitones.
double LfoPitchError(uint8_t shape, bool free_run, double* measured_st) {
  Patch patch = { ENGINE_BT, 2, { 0, 0, 32768, 0 }, { 0, 0, 0, 0 },
                  { 0, 65535, 0, 0 }, shape, free_run };
  double rate = shape == LFO_RAMP_UP ? 1.2 : 2.4;
  double expected = (free_run ? -12.0 : 0.0) + rate * 0.7;
  double hz = MeasurePitch(patch, 0);
  *measured_st = 12.0 * log2(hz / 65.40639133);
  return fabs(*measured_st - expected);
}

// LFO knob maths: speed 0.05 / 1 / 20 Hz, depth off / 12 st, and the wave
// shapes at their turning points.
bool LfoParametersOk() {
  const uint16_t knob[3] = { 0, 32768, 65535 };
  const double hz[3] = { 0.05, 1.0, 20.0 };
  bool ok = true;
  for (int i = 0; i < 3; ++i) {
    uint16_t lfo[4] = { knob[i], knob[i], 0, 0 };
    uint16_t main_layer[4] = { 9146, 40632, 32768, 19661 };
    uint16_t second[4] = { 0, 16384, 0, 0 };
    VoiceParameters p;
    ComputeVoiceParameters(ENGINE_BT, 2, main_layer, second, lfo, LFO_TRIANGLE, &p);
    double f = p.lfo_increment * kFirmwareRate / 4294967296.0;
    ok = ok && fabs(f / hz[i] - 1.0) < 0.01;
    if (i == 0) ok = ok && p.lfo_depth == 0;
    if (i == 2) ok = ok && p.lfo_depth == 12 * 65536;
  }
  ok = ok && Voice::LfoWave(0, LFO_TRIANGLE) == -32768 &&
       Voice::LfoWave(0x7fffffffUL, LFO_TRIANGLE) == 32767 &&
       Voice::LfoWave(0x80000000UL, LFO_TRIANGLE) == 32767 &&
       Voice::LfoWave(0xffffffffUL, LFO_TRIANGLE) == -32768 &&
       Voice::LfoWave(0x40000000UL, LFO_TRIANGLE) == 0 &&
       Voice::LfoWave(0, LFO_RAMP_UP) == -32768 &&
       Voice::LfoWave(0x80000000UL, LFO_RAMP_UP) == 0 &&
       Voice::LfoWave(0xffffffffUL, LFO_RAMP_UP) == 32767;
  return ok;
}

// Restart on trigger: right after any trigger the LFO is at the middle of
// its wave, wherever it was before. Free running: a trigger leaves it alone.
bool LfoRestartOk() {
  uint16_t main_layer[4] = { 9146, 40632, 32768, 19661 };
  uint16_t second[4] = { 0, 16384, 0, 0 };
  uint16_t lfo[4] = { 50000, 40000, 0, 0 };
  bool ok = true;
  for (uint8_t shape = 0; shape < LFO_SHAPE_LAST; ++shape) {
    for (int free_run = 0; free_run < 2; ++free_run) {
      VoiceParameters p;
      ComputeVoiceParameters(ENGINE_BT, 2, main_layer, second, lfo, shape, &p,
                             free_run != 0);
      Voice voice;
      voice.Init();
      uint16_t gain, frequency;
      for (int hit = 0; hit < 3; ++hit) {
        for (int n = 0; n < 10007 * (hit + 1); ++n) {
          voice.Process(p, false, 0, 0, &gain, &frequency);
        }
        uint32_t before = voice.lfo_phase();
        voice.Process(p, true, 0, 0, &gain, &frequency);
        uint32_t expected = (free_run ? before : Voice::LfoStart(shape)) +
                            static_cast<uint32_t>(p.lfo_increment);
        ok = ok && voice.lfo_phase() == expected;
      }
      ok = ok && Voice::LfoWave(Voice::LfoStart(shape), shape) == 0;
    }
  }
  return ok;
}

// Pitch sweep (TEST, METER + SHAPE 2, the LFO layer's slot 3). Off must not
// change the sound. At full: the first 50 ms of a C2 hit has more zero
// crossings than without (it starts 12 st up), and 0.2-1.2 s in the pitch is
// back on C2 (the held note stays in tune). Returns the error in cents.
double SweepTailCents(bool* start_higher, bool* off_same) {
  Patch base = { ENGINE_BT, 2, { 65535, 65535, 32768, 0 }, { 0, 0, 0, 0 },
                 { 0, 0, 0, 0 } };
  Patch sweep = base;
  sweep.lfo_layer[3] = 65535;
  VoiceParameters p;
  ComputeVoiceParameters(ENGINE_BT, 2, base.main_layer, base.second_layer,
                         base.lfo_layer, 0, &p);
  *off_same = p.sweep_depth == 0;
  int crossings[2];
  for (int k = 0; k < 2; ++k) {
    const Patch& pt = k ? sweep : base;
    ComputeVoiceParameters(ENGINE_BT, 0, pt.main_layer, pt.second_layer,
                           pt.lfo_layer, 0, &p);
    Voice voice;
    voice.Init();
    uint16_t g, f;
    for (int i = 0; i < 3 * kFirmwareRate; ++i) voice.Process(p, false, 0, 0, &g, &f);
    int prev = 0, count = 0;
    for (int i = 0; i < static_cast<int>(0.05 * kFirmwareRate); ++i) {
      voice.Process(p, i == 0, 12 << 16, 0, &g, &f);
      int v = static_cast<int>(g) - 32767;
      if (prev < 0 && v >= 0) ++count;
      prev = v;
    }
    crossings[k] = count;
  }
  *start_higher = crossings[1] > crossings[0] + 1;
  double hz = MeasurePitch(sweep, 0);
  return 1200.0 * log2(hz / 65.40639133);
}

// Depth at zero: the speed knob must not change the sound at all.
bool LfoOffIsSilent(const std::vector<Step>& steps) {
  Patch slow = { ENGINE_BT, 2, { 9146, 40632, 32768, 19661 },
                 { 0, 16384, 0, 0 }, { 0, 0, 0, 0 }, LFO_TRIANGLE };
  Patch fast = slow;
  fast.lfo_layer[0] = 65535;
  fast.lfo_shape = LFO_RAMP_UP;
  return Render(slow, steps, 122.0, 1).raw ==
         Render(fast, steps, 122.0, 1).raw;
}

// Soft distortion (MOD 2), read from the voice's own VCA words with snap,
// noise off. One hit of C2; over the first 8 cycles, the 3rd
// harmonic against the fundamental; in the tail (0.45 to 0.55 s, where the
// note is about 25 dB down), the level against MOD 2 fully CCW. The tail must
// get the small-signal gain of the drive (1 + 3 x knob^2) times the level
// correction after the clipper, the start of the hit must get more 3rd
// harmonic as the knob goes up.
struct DistortionResult { double h3_db, tail_db; int32_t peak; };

DistortionResult MeasureDistortion(uint8_t engine, uint16_t knob) {
  uint16_t main_layer[4] = { 9146, 65535, 32768, knob };
  uint16_t second[4] = { 0, 0, 0, 0 }, lfo[4] = { 0, 0, 0, 0 };
  VoiceParameters p;
  ComputeVoiceParameters(engine, 0, main_layer, second, lfo, LFO_TRIANGLE, &p);
  Voice voice;
  voice.Init();
  uint16_t g, f;
  for (int i = 0; i < 3 * kFirmwareRate; ++i) voice.Process(p, false, 0, 0, &g, &f);
  const double f0 = 65.40639133;
  const int cycles = static_cast<int>(8 * kFirmwareRate / f0 + 0.5);
  const int tail_a = static_cast<int>(0.45 * kFirmwareRate);
  const int tail_b = static_cast<int>(0.55 * kFirmwareRate);
  double re1 = 0, im1 = 0, re3 = 0, im3 = 0, tail = 0;
  DistortionResult r = { 0, 0, 0 };
  for (int n = 0; n < tail_b; ++n) {
    voice.Process(p, n == 0, 0, 0, &g, &f);  // 0 V: C2
    int32_t v = static_cast<int32_t>(g) - 32767;
    if (abs(v) > r.peak) r.peak = abs(v);
    if (n < cycles) {
      double w = 2 * M_PI * f0 * n / kFirmwareRate;
      re1 += v * cos(w); im1 += v * sin(w);
      re3 += v * cos(3 * w); im3 += v * sin(3 * w);
    }
    if (n >= tail_a) tail += static_cast<double>(v) * v;
  }
  r.h3_db = 10 * log10((re3 * re3 + im3 * im3) / (re1 * re1 + im1 * im1));
  r.tail_db = 10 * log10(tail / (tail_b - tail_a));
  return r;
}

bool DistortionOk(uint8_t engine) {
  const uint16_t knobs[3] = { 0, 32768, 65535 };
  double expected_tail[3] = { 0.0, 20 * log10(1.75), 20 * log10(4.0) };
  DistortionResult r[3];
  uint16_t zero[4] = { 0, 0, 0, 0 };
  VoiceParameters p;
  for (int i = 0; i < 3; ++i) {
    r[i] = MeasureDistortion(engine, knobs[i]);
    uint16_t layer[4] = { 9146, 65535, 32768, knobs[i] };
    ComputeVoiceParameters(engine, 0, layer, zero, zero, LFO_TRIANGLE, &p);
    if (i) expected_tail[i] += 20 * log10(p.distortion_gain / 4096.0);
  }
  uint16_t off_layer[4] = { 9146, 65535, 32768, 0 };
  ComputeVoiceParameters(engine, 0, off_layer, zero, zero, LFO_TRIANGLE, &p);
  bool ok = p.distortion == 0 && r[1].h3_db > r[0].h3_db + 1 &&
            r[2].h3_db > r[1].h3_db + 1 && r[2].h3_db > r[0].h3_db + 5;
  printf("Distortion %s (MOD 2 off / noon / full): 3rd harmonic", engine == ENGINE_BT ? "BT" : "LT");
  for (int i = 0; i < 3; ++i) printf(" %.1f", r[i].h3_db);
  printf(" dB, tail");
  for (int i = 0; i < 3; ++i) {
    double gain = r[i].tail_db - r[0].tail_db;
    bool tail_ok = fabs(gain - expected_tail[i]) < 0.5;
    ok = ok && tail_ok && r[i].peak <= 32767;
    printf(" %+.1f", gain);
  }
  printf(" dB (expected +0.0 %+.1f %+.1f), peak word %d %d %d  %s\n",
         expected_tail[1], expected_tail[2], r[0].peak, r[1].peak, r[2].peak,
         ok ? "OK" : "FAIL");
  return ok;
}

// Wave (SHAPE 2), read from the voice's own VCA words with snap,
// noise and distortion off. One hit of C2, the first 8 cycles:
// harmonics 1 to 16 against the plain sine. Fully CCW must leave the voice
// untouched; each stage must sound like its name while the fundamental
// stays within 6 dB (the bass must not disappear): the triangle's 5th
// harmonic near 1/25, the folded wave far brighter, and fully CW square-like:
// odd harmonics, 3rd near 1/3 (-9.5 dB), even ones low.
struct WaveResult { double h[17]; int32_t peak; };

WaveResult MeasureWave(uint8_t engine, uint16_t knob, double start_s = 0,
                       uint16_t decay = 9146) {
  uint16_t main_layer[4] = { decay, 65535, 32768, 0 };
  uint16_t second[4] = { 0, 0, knob, 0 }, lfo[4] = { 0, 0, 0, 0 };
  VoiceParameters p;
  ComputeVoiceParameters(engine, 0, main_layer, second, lfo, LFO_TRIANGLE, &p);
  Voice voice;
  voice.Init();
  uint16_t g, f;
  for (int i = 0; i < 3 * kFirmwareRate; ++i) voice.Process(p, false, 0, 0, &g, &f);
  const double f0 = 65.40639133;
  const int cycles = static_cast<int>(8 * kFirmwareRate / f0 + 0.5);
  const int start = static_cast<int>(start_s * kFirmwareRate);
  double re[17] = { 0 }, im[17] = { 0 };
  WaveResult r;
  r.peak = 0;
  for (int n = 0; n < start + cycles; ++n) {
    voice.Process(p, n == 0, 0, 0, &g, &f);  // 0 V: C2
    if (n < start) continue;
    int32_t v = static_cast<int32_t>(g) - 32767;
    if (abs(v) > r.peak) r.peak = abs(v);
    double w = 2 * M_PI * f0 * n / kFirmwareRate;
    for (int k = 1; k <= 16; ++k) {
      re[k] += v * cos(k * w);
      im[k] += v * sin(k * w);
    }
  }
  for (int k = 1; k <= 16; ++k) r.h[k] = re[k] * re[k] + im[k] * im[k];
  return r;
}

// Wave CV: ui.cc sends the voice only the wave fields when the LEVEL 2 CV
// moves (ComputeWaveParameters). They must be the same as the full
// ComputeVoiceParameters gives for that wave, all along the knob, and the
// call must leave every other field alone.
bool WaveCvOk() {
  uint16_t main_layer[4] = { 9146, 65535, 32768, 20000 }, zero[4] = { 0, 0, 0, 0 };
  bool same = true, others_kept = true;
  for (int32_t w = 0; w <= 65535; w += 257) {  // 0 .. 65535
    uint16_t second_layer[4] = { 0, 0, static_cast<uint16_t>(w), 0 };
    VoiceParameters full, part, before;
    ComputeVoiceParameters(ENGINE_BT, 2, main_layer, second_layer, zero, LFO_TRIANGLE, &full);
    memset(&part, 0x5a, sizeof(part));
    before = part;
    ComputeWaveParameters(w, &part);
    same = same && part.wave_tri == full.wave_tri && part.wave_fold == full.wave_fold &&
           part.fold_gain == full.fold_gain && part.wave_square == full.wave_square &&
           part.wave_gain == full.wave_gain;
    part.wave_tri = before.wave_tri;
    part.wave_fold = before.wave_fold;
    part.fold_gain = before.fold_gain;
    part.wave_square = before.wave_square;
    part.wave_gain = before.wave_gain;
    others_kept = others_kept && memcmp(&part, &before, sizeof(part)) == 0;
  }
  bool ok = same && others_kept;
  printf("Wave CV: wave fields alone = full parameters at 256 points along the knob, "
         "other fields untouched  %s\n", ok ? "OK" : "FAIL");
  return ok;
}

bool WaveOk(uint8_t engine) {
  const uint16_t knobs[4] = { 0, 21845, 43690, 65535 };  // off, triangle, folded, square
  const char* names[4] = { "sine", "triangle", "folded", "square" };
  WaveResult r[4];
  for (int i = 0; i < 4; ++i) r[i] = MeasureWave(engine, knobs[i]);
  uint16_t main_layer[4] = { 9146, 65535, 32768, 0 }, zero[4] = { 0, 0, 0, 0 };
  VoiceParameters p;
  ComputeVoiceParameters(engine, 0, main_layer, zero, zero, LFO_TRIANGLE, &p);
  bool ok = p.wave_tri == 0 && p.wave_fold == 0 && p.wave_square == 0;
  double upper[4], fund[4];
  printf("Wave %s: fundamental / harmonics 2-16 (dB against the sine's fundamental):\n",
         engine == ENGINE_BT ? "BT" : "LT");
  for (int i = 0; i < 4; ++i) {
    double sum = 0;
    for (int k = 2; k <= 16; ++k) sum += r[i].h[k];
    fund[i] = 10 * log10(r[i].h[1] / r[0].h[1]);
    upper[i] = 10 * log10(sum / r[0].h[1]);
    printf("  %-8s %+5.1f / %+6.1f dB, peak word %d\n", names[i], fund[i], upper[i], r[i].peak);
    ok = ok && fund[i] > -6.0 && r[i].peak <= 32767;
  }
  // The triangle is no brighter than the sine, which the drive already
  // clips a little; it is told apart by its 5th harmonic, in the body of a
  // long note (decay 2.5 s, 1 s in, steady enough for a clean measurement):
  // the level matching clips its tips at the start of a hit.
  WaveResult sine_body = MeasureWave(engine, knobs[0], 1.0, 65535);
  WaveResult tri_body = MeasureWave(engine, knobs[1], 1.0, 65535);
  double h5 = 10 * log10(tri_body.h[5] / tri_body.h[1]);
  double h5_sine = 10 * log10(sine_body.h[5] / sine_body.h[1]);
  double h3 = 10 * log10(r[3].h[3] / r[3].h[1]);
  double even = 10 * log10((r[3].h[2] + r[3].h[4] + r[3].h[6]) / r[3].h[1]);
  ok = ok && h5 > h5_sine + 2.0 && upper[2] > upper[0] + 6 &&
       upper[2] > upper[1] + 6 && upper[3] > upper[1] + 6 &&
       h3 > -13.0 && h3 < -7.0 && even < -25.0;
  printf("  triangle: 5th harmonic %.1f dB (sine %.1f); square: 3rd harmonic "
         "%.1f dB (expected about -9.5), even harmonics %.1f dB  %s\n",
         h5, h5_sine, h3, even, ok ? "OK" : "FAIL");
  return ok;
}

// Wave level: RMS of the VCA words over the first 0.5 s of a C2 note (+1
// every 1/32 of the Wave knob, against Wave off.
// Must stay within 1 dB: no dip at the triangle, no jump at the square.
bool WaveLevelOk(uint8_t engine) {
  double ref = 0, lo = 0, hi = 0;
  for (int k = 0; k <= 32; ++k) {
    uint16_t knob = k == 32 ? 65535 : k * 2048;
    uint16_t main_layer[4] = { 65535, 65535, 32768, 0 };
    uint16_t second[4] = { 0, 0, knob, 0 }, lfo[4] = { 0, 0, 0, 0 };
    VoiceParameters p;
    ComputeVoiceParameters(engine, 2, main_layer, second, lfo, LFO_TRIANGLE, &p);
    Voice voice;
    voice.Init();
    uint16_t g, f;
    for (int i = 0; i < 3 * kFirmwareRate; ++i) voice.Process(p, false, 0, 0, &g, &f);
    double sum = 0;
    const int n = static_cast<int>(0.5 * kFirmwareRate);
    for (int i = 0; i < n; ++i) {
      voice.Process(p, i == 0, 0, 0, &g, &f);
      double v = static_cast<double>(g) - 32767;
      sum += v * v;
    }
    double db = 10 * log10(sum / n);
    if (k == 0) ref = db;
    lo = fmin(lo, db - ref);
    hi = fmax(hi, db - ref);
  }
  bool ok = lo > -1.0 && hi < 1.0;
  printf("Wave level %s, along the knob against off: %+.1f to %+.1f dB  %s\n",
         engine == ENGINE_BT ? "BT" : "LT", lo, hi, ok ? "OK" : "FAIL");
  return ok;
}

// Distortion level: RMS of the VCA words over the first 0.3 s of a C2 hit,
// MOD 2 every 1/32 against fully CCW, for Decay every 1/8 and Wave off /
// noon / full. Must stay within 1 dB: turning up MOD 2 changes the colour,
// not the loudness.
bool DistortionLevelOk(uint8_t engine) {
  double lo = 0, hi = 0;
  for (int d = 0; d <= 8; ++d) {
    for (int w = 0; w < 3; ++w) {
      double ref = 0;
      for (int k = 0; k <= 32; ++k) {
        uint16_t knob = k == 32 ? 65535 : k * 2048;
        uint16_t main_layer[4] = {
            static_cast<uint16_t>(d == 8 ? 65535 : d * 8192), 65535, 32768, knob };
        uint16_t second[4] = {
            0, 0, static_cast<uint16_t>(w == 2 ? 65535 : w * 32768), 0 };
        uint16_t lfo[4] = { 0, 0, 0, 0 };
        VoiceParameters p;
        ComputeVoiceParameters(engine, 2, main_layer, second, lfo, LFO_TRIANGLE, &p);
        Voice voice;
        voice.Init();
        uint16_t g, f;
        for (int i = 0; i < 3 * kFirmwareRate; ++i) voice.Process(p, false, 0, 0, &g, &f);
        double sum = 0;
        const int n = static_cast<int>(0.3 * kFirmwareRate);
        for (int i = 0; i < n; ++i) {
          voice.Process(p, i == 0, 0, 0, &g, &f);
          double v = static_cast<double>(g) - 32767;
          sum += v * v;
        }
        double db = 10 * log10(sum / n);
        if (k == 0) ref = db;
        lo = fmin(lo, db - ref);
        hi = fmax(hi, db - ref);
      }
    }
  }
  bool ok = lo > -1.0 && hi < 1.0;
  printf("Distortion level %s, MOD 2 along the knob against off (all decays, wave off / noon / full): %+.1f to %+.1f dB  %s\n",
         engine == ENGINE_BT ? "BT" : "LT", lo, hi, ok ? "OK" : "FAIL");
  return ok;
}

// Crush (METER + SHAPE 1), read from the voice's own VCA words with snap,
// distortion off. One hit of C2, 0.1 to 0.6 s. Off: the words
// change on most samples. Just past off: about 6 kHz. Noon: about 2.1 kHz.
// Full: about 750 Hz. The level must stay, and within full scale.
struct CrushResult { double changes, rms_db; int32_t peak; };

CrushResult MeasureCrush(uint8_t engine, uint16_t knob) {
  uint16_t main_layer[4] = { 40000, 65535, 32768, 0 };
  uint16_t second[4] = { knob, 0, 0, 0 }, lfo[4] = { 0, 0, 0, 0 };
  VoiceParameters p;
  ComputeVoiceParameters(engine, 0, main_layer, second, lfo, LFO_TRIANGLE, &p);
  Voice voice;
  voice.Init();
  uint16_t g, f;
  for (int i = 0; i < 3 * kFirmwareRate; ++i) voice.Process(p, false, 0, 0, &g, &f);
  CrushResult r = { 0, 0, 0 };
  const int a = static_cast<int>(0.1 * kFirmwareRate), b = static_cast<int>(0.6 * kFirmwareRate);
  int32_t last = 0;
  double sum = 0;
  int changes = 0;
  for (int n = 0; n < 3 * kFirmwareRate; ++n) {
    voice.Process(p, n == 0, 0, 0, &g, &f);
    int32_t v = static_cast<int32_t>(g) - 32767;
    if (abs(v) > r.peak) r.peak = abs(v);
    if (n >= a && n < b) {
      if (v != last) ++changes;
      sum += static_cast<double>(v) * v;
    }
    last = v;
  }
  r.changes = static_cast<double>(changes) / (b - a);
  r.rms_db = 10 * log10(sum / (b - a));
  return r;
}

bool CrushOk(uint8_t engine) {
  CrushResult off = MeasureCrush(engine, 0);
  CrushResult start = MeasureCrush(engine, 700);
  CrushResult mid = MeasureCrush(engine, 32768);
  CrushResult full = MeasureCrush(engine, 65535);
  uint16_t main_layer[4] = { 40000, 65535, 32768, 0 }, zero[4] = { 0, 0, 0, 0 };
  VoiceParameters p;
  ComputeVoiceParameters(engine, 0, main_layer, zero, zero, LFO_TRIANGLE, &p);
  double hz[3] = { start.changes * kFirmwareRate, mid.changes * kFirmwareRate,
                   full.changes * kFirmwareRate };
  const double expected[3] = { 6000 * pow(2, -3 * 700 / 65535.0), 6000 * pow(2, -1.5), 750 };
  bool ok = p.crush_increment == 0 && off.changes > 0.9 &&
            full.peak <= 32767 && mid.peak <= 32767 && start.peak <= 32767;
  for (int i = 0; i < 3; ++i) ok = ok && fabs(hz[i] / expected[i] - 1) < 0.05;
  ok = ok && fabs(mid.rms_db - off.rms_db) < 1.0 && fabs(full.rms_db - off.rms_db) < 1.0;
  printf("Crush %s (just past off / noon / full): sample rate %.0f / %.0f / %.0f Hz "
         "(expected %.0f / %.0f / %.0f), level %+.1f / %+.1f dB  %s\n",
         engine == ENGINE_BT ? "BT" : "LT", hz[0], hz[1], hz[2], expected[0], expected[1],
         expected[2], mid.rms_db - off.rms_db, full.rms_db - off.rms_db, ok ? "OK" : "FAIL");
  return ok;
}

// Tone fully closed must not silence the note: RMS of OUT 1 (AC-coupled,
// 3 Hz high-pass) over the first second of a note, Tone fully CCW against
// fully CW. Returns the ratio in dB. A 1 s rest first lets the filter
// control voltage settle from power-up.
double NoteRms(Patch patch, int note) {
  std::vector<Step> steps = { {-1}, {note} };
  Result r = Render(patch, steps, 15.0, 1);  // 1 s per step
  double x1 = r.raw[0], y1 = 0, k = 1.0 - 2 * M_PI * 3.0 / kHostRate, sum = 0;
  size_t n = static_cast<size_t>(kHostRate);
  for (size_t i = 0; i < 2 * n; ++i) {
    double y = r.raw[i] - x1 + k * y1;
    x1 = r.raw[i]; y1 = y;
    if (i >= n) sum += y * y;
  }
  return sqrt(sum / n);
}

double ToneClosedDb(Patch patch, int note) {
  patch.main_layer[1] = 65535;
  double open = NoteRms(patch, note);
  patch.main_layer[1] = 0;
  double closed = NoteRms(patch, note);
  return 20.0 * log10(closed / open);
}


// Retrigger click. A line of 8 notes, 125 ms apart, with SHAPE 1 (decay) at
// 50 / 75 / 100 %, so each note is still loud when the next one starts.
// OUT 1 goes through a 3 Hz high-pass, like an audio input. Measures the
// largest one-sample jump in the 1 ms after each trigger: the first note
// (from silence) against the worst retrigger, within 20 %. Also checks that
// from 5 ms on, each note's peak-to-peak swing is within 5 % of the same note
// played alone (no stacking).
std::vector<double> HighPass(const std::vector<float>& volts) {
  std::vector<double> out(volts.size());
  double x1 = volts.empty() ? 0 : volts[0], y1 = 0;
  double r = 1.0 - 2 * M_PI * 3.0 / kHostRate;
  for (size_t i = 0; i < volts.size(); ++i) {
    double y = volts[i] - x1 + r * y1;
    x1 = volts[i]; y1 = y;
    out[i] = y;
  }
  return out;
}

double LargestJump(const std::vector<double>& y, size_t start, size_t length) {
  double worst = 0;
  for (size_t i = start + 1; i < start + length && i < y.size(); ++i) {
    worst = fmax(worst, fabs(y[i] - y[i - 1]));
  }
  return worst;
}

double PeakToPeak(const std::vector<float>& v, size_t start, size_t length) {
  double lo = 1e9, hi = -1e9;
  for (size_t i = start; i < start + length && i < v.size(); ++i) {
    lo = fmin(lo, v[i]);
    hi = fmax(hi, v[i]);
  }
  return hi - lo;
}

bool RetriggerOk(uint8_t engine, uint16_t decay) {
  Patch patch = { engine, 2, { decay, 32768, 32768, 19660 },
                  { 0, 16384, 0, 0 } };
  std::vector<Step> line = { {0}, {0}, {12}, {0}, {7}, {0}, {10}, {12} };
  const double bpm = 120.0;  // 125 ms per step
  Result r = Render(patch, line, bpm, 1);
  std::vector<double> y = HighPass(r.raw);
  double samples_per_step = kFirmwareRate * 60.0 / bpm / 4.0;
  size_t ms = static_cast<size_t>(kHostRate / 1000.0);
  double from_silence = 0, worst = 0, worst_level = 0;
  bool level_ok = true;
  for (size_t k = 0; k < line.size(); ++k) {
    // Host sample of the first output of the trigger sample.
    int64_t n = static_cast<int64_t>(ceil(k * samples_per_step));
    size_t start = static_cast<size_t>(floor(n * kHostRate / kFirmwareRate));
    double jump = LargestJump(y, start, ms);
    if (k == 0) from_silence = jump; else worst = fmax(worst, jump);
    // Same note alone.
    std::vector<Step> alone(1, line[k]);
    Result ra = Render(patch, alone, bpm, 1);
    size_t length = static_cast<size_t>(samples_per_step * kHostRate / kFirmwareRate) - 6 * ms;
    double a = PeakToPeak(ra.raw, 5 * ms, length);
    double b = PeakToPeak(r.raw, start + 5 * ms, length);
    double level = fabs(b / a - 1.0);
    worst_level = fmax(worst_level, level);
    level_ok = level_ok && level < 0.05;
  }
  // From silence the new note starts alone; on a retrigger the old note's
  // 1.5 ms fade adds its own slope, about 10 % more at full decay. Until the
  // fixed lift, the from-silence figure also held the lift's jump (BT 0.51 V,
  // retrigger 0.45 V), so the limit was x1.0; now BT is 0.32 V / 0.35 V.
  bool ok = worst <= from_silence * 1.2 && level_ok;
  printf("  %s decay %3d %%: from silence %.3f V, worst retrigger %.3f V (x%.1f), "
         "level after 5 ms within %.1f %%  %s\n",
         engine == ENGINE_BT ? "BT" : "LT", (decay * 100 + 32767) / 65535,
         from_silence, worst, worst / from_silence, worst_level * 100,
         ok ? "OK" : "FAIL");
  return ok;
}

// Knob jump click. When a knob takes over a value (tomline/pickup.h), the
// value goes from the stored one to the knob's at most kPickupSlew per ms
// (main loop rate). A long note plays; 300 ms in, one value goes from one
// end to the other, worst case: the knob is already at the far end. Measures
// the largest one-sample jump in the 100 ms after it against the same note
// played at the start value and at the end value (the sound's own steps).
uint16_t* JumpValue(Patch* patch);
int jump_knob = 0;
bool jump_glide = false;
int32_t jump_from = 0, jump_to = 0;
Pickup jump_pickup;
const int64_t kJumpAt = static_cast<int64_t>(0.3 * kFirmwareRate);

uint16_t* JumpValue(Patch* patch) {
  if (jump_knob < 4) return &patch->main_layer[jump_knob];
  if (jump_knob < 8) return &patch->second_layer[jump_knob - 4];
  return &patch->lfo_layer[jump_knob - 8];
}

// The page opens 100 ms before the jump, with the knob at the far end.
const int64_t kPageAt = kJumpAt - static_cast<int64_t>(0.1 * kFirmwareRate);

bool JumpControl(int64_t n, Patch* patch) {
  uint16_t* value = JumpValue(patch);
  if (!jump_glide) {
    if (n != kJumpAt) return false;
    *value = jump_to;
    return true;
  }
  if (n == kPageAt) jump_pickup.Hold(jump_from);
  // Main loop: once per ms. The knob is turned at kJumpAt.
  if (n >= kPageAt && n % 31 == 0) {
    return jump_pickup.Follow(n < kJumpAt ? jump_from : jump_to, 1, value);
  }
  return false;
}

double JumpLargest(const Patch& patch, Control control) {
  std::vector<Step> note(1, Step{12});
  Result r = Render(patch, note, 30.0, 1);
  if (control) r = Render(patch, note, 30.0, 1, control);
  std::vector<double> y = HighPass(r.raw);
  size_t start = static_cast<size_t>(kJumpAt * kHostRate / kFirmwareRate);
  return LargestJump(y, start, static_cast<size_t>(0.1 * kHostRate));
}

bool KnobJumpOk(uint8_t engine, int knob, const char* name, int32_t from, int32_t to) {
  Patch patch = { engine, 2, { 65535, 32768, 32768, 19661 },
                  { 0, 16384, 0, 0 }, { 49152, 0, 0, 0 } };
  jump_knob = knob;
  jump_from = from;
  jump_to = to;
  *JumpValue(&patch) = from;
  double before = JumpLargest(patch, NULL);
  Patch end = patch;
  *JumpValue(&end) = to;
  double after = JumpLargest(end, NULL);
  double reference = fmax(before, after);
  // The settings the glide passes through count too: the folded wave, half
  // way along the wave knob, is spikier than both ends.
  for (int i = 1; i < 12; ++i) {
    Patch mid = patch;
    *JumpValue(&mid) = from + (to - from) * i / 12;
    reference = fmax(reference, JumpLargest(mid, NULL));
  }
  jump_glide = false;
  double instant = JumpLargest(patch, JumpControl);
  jump_glide = true;
  double glide = JumpLargest(patch, JumpControl);
  bool ok = glide <= reference * 1.1;
  printf("  %s %-10s %3d -> %3d %%: sound alone %.3f V, instant jump %.3f V, "
         "glide %.3f V  %s\n", engine == ENGINE_BT ? "BT" : "LT", name,
         (from * 100 + 32767) / 65535, (to * 100 + 32767) / 65535, reference,
         instant, glide, ok ? "OK" : "FAIL");
  return ok;
}

// Snap off (snap 0): no click noise and no pitch blip for either engine, and
// the largest one-sample jump in the first 3 ms of a hit (AC-coupled) must be
// smaller than with the 0.4 ms snap.
bool SnapOffOk() {
  bool ok = true;
  for (uint8_t engine = 0; engine < ENGINE_LAST; ++engine) {
    uint16_t main_layer[4] = { 9146, 65535, 32768, 0 }, zero[4] = { 0, 0, 0, 0 };
    VoiceParameters p;
    ComputeVoiceParameters(engine, 0, main_layer, zero, zero, LFO_TRIANGLE, &p);
    ok = ok && p.snap_noise == 0 && p.snap_depth == 0;
    double jump[2];
    for (uint8_t snap = 0; snap < 2; ++snap) {
      Patch patch = { engine, snap, { 9146, 65535, 32768, 0 }, { 0, 0, 0, 0 } };
      std::vector<Step> note(1, Step{12});
      Result r = Render(patch, note, 30.0, 1);
      std::vector<double> y = HighPass(r.raw);
      jump[snap] = LargestJump(y, 0, static_cast<size_t>(0.003 * kHostRate));
    }
    ok = ok && jump[0] < jump[1];
    printf("Snap off %s: largest jump in the first 3 ms %.3f V (0.4 ms snap: %.3f V)\n",
           engine == ENGINE_BT ? "BT" : "LT", jump[0], jump[1]);
  }
  return ok;
}

// Attack jump: the average of OUT 1 over the 100 ms after a hit, minus the
// average over the 100 ms before it. A lift that follows each note makes this
// about 2.4 V, a step the Tone filter can't remove (heard as a click on every
// hit). A plain decaying sine gives about 0.1 V on its own.
double AttackJumpVolts(const Patch& patch) {
  std::vector<Step> steps = { {-1}, {-1}, {-1}, {-1}, {0} };
  Result r = Render(patch, steps, 120.0, 1);  // hit at 0.5 s
  size_t hit = static_cast<size_t>(0.5 * kHostRate);
  size_t w = static_cast<size_t>(0.1 * kHostRate);
  double before = 0, after = 0;
  for (size_t i = 0; i < w; ++i) {
    before += r.raw[hit - w + i];
    after += r.raw[hit + i];
  }
  return (after - before) / w;
}

// Power-up: VCA word at 0 for the first second (Ui::CheckSetup reads LEVEL
// MOD 1 there), then a rise of one step per sample to 32767, never faster.
bool PowerUpOk() {
  uint16_t main_layer[4] = { 9146, 40632, 32768, 19661 };
  uint16_t second[4] = { 0, 16384, 0, 0 }, lfo[4] = { 0, 0, 0, 0 };
  VoiceParameters p;
  ComputeVoiceParameters(ENGINE_BT, 2, main_layer, second, lfo, LFO_TRIANGLE, &p);
  Voice voice;
  voice.Init();
  uint16_t g, f, prev = 0;
  bool ok = true;
  for (int i = 0; i < 3 * kFirmwareRate; ++i) {
    voice.Process(p, false, 0, 0, &g, &f);
    if (i < 31088) ok = ok && g == 0;
    ok = ok && g >= prev && g - prev <= 1;
    prev = g;
  }
  return ok && g == 32767;
}

// --- Sidechain duck (IN 2) ---------------------------------------------------

// A kick like the browser page's: sine, pitch 158 Hz falling to 48 Hz (30 ms),
// level falling with a 280 ms time constant, `peak` volts.
double KickVolts(double t, double peak = 6.0) {
  if (t < 0) return 0;
  double pe = exp(-t / 0.03);
  double phase = 48.0 * t + 110.0 * 0.03 * (1.0 - pe);
  return peak * exp(-t / 0.28) * sin(2 * M_PI * phase);
}

// IN 2 counts. The real scaling of IN 2 is not measured: the EXCITE scale
// (3972 counts per volt) is assumed. Inverted, like CvScaler::audio_sample.
int32_t KickCounts(double volts) {
  double c = -volts * kCountsPerVolt;
  if (c > 32767) c = 32767;
  if (c < -32768) c = -32768;
  return static_cast<int32_t>(c);
}

const double kKickBpm = 122.0;
int32_t KickOnBeats(int64_t n) {  // a kick every beat (4 steps)
  double beat = 60.0 / kKickBpm;
  double t = n / kFirmwareRate;
  return KickCounts(KickVolts(fmod(t, beat)));
}

struct DuckRun {
  std::vector<int32_t> wave;   // VCA word minus the lift
  std::vector<int32_t> level;  // Voice::duck_level()
};

// Firmware-rate run: optional note at sample 0, sidechain from `in2`.
DuckRun RunDuck(uint16_t depth, uint16_t release, bool note,
                int32_t (*in2)(int64_t), int samples, int32_t warm_in2 = 0) {
  uint16_t main_layer[4] = { 65535, 65535, 32768, 0 };  // long decay, open
  uint16_t second[4] = { 0, 0, 0, 0 }, lfo[4] = { 32768, 0, 0, 0 };
  uint16_t duck[2] = { depth, release };
  VoiceParameters p;
  ComputeVoiceParameters(ENGINE_BT, 0, main_layer, second, lfo, LFO_TRIANGLE,
                         &p, false, duck);
  Voice voice;
  voice.Init();
  uint16_t g, f;
  for (int i = 0; i < 3 * kFirmwareRate; ++i) {
    voice.Process(p, false, 0, warm_in2, &g, &f);
  }
  DuckRun r;
  for (int i = 0; i < samples; ++i) {
    voice.Process(p, note && i == 0, 0, in2 ? in2(i) : warm_in2, &g, &f);
    r.wave.push_back(static_cast<int32_t>(g) - 32767);
    r.level.push_back(voice.duck_level());
  }
  return r;
}

double RmsOf(const std::vector<int32_t>& x, size_t a, size_t b) {
  double sum = 0;
  for (size_t i = a; i < b; ++i) sum += static_cast<double>(x[i]) * x[i];
  return sqrt(sum / (b - a));
}

int32_t KickAt100ms(int64_t n) {  // one kick, 100 ms in
  return KickCounts(KickVolts((n - 3109) / kFirmwareRate));
}
int32_t ShortKick(int64_t n) {  // one 6 V kick dying away in 20 ms, 100 ms in
  double t = (n - 3109) / kFirmwareRate;
  return t < 0 ? 0 : KickCounts(6.0 * exp(-t / 0.02) * sin(2 * M_PI * 55.0 * t));
}

bool DuckOk() {
  const int len = static_cast<int>(kFirmwareRate);  // 1 s
  const int hit = 3109;
  bool all = true;
  // Depth at zero: the duck changes nothing, even with a kick on IN 2.
  DuckRun plain = RunDuck(0, 32768, true, NULL, len);
  DuckRun off = RunDuck(0, 32768, true, KickAt100ms, len);
  bool off_ok = plain.wave == off.wave;
  printf("  depth at zero, kick on IN 2: VCA words identical to no kick  %s\n",
         off_ok ? "OK" : "FAIL");
  all = all && off_ok;
  // Full depth, IN 2 empty (steady level after power-up): no duck.
  DuckRun empty = RunDuck(65535, 32768, true, NULL, len, -13227);  // +3.33 V
  bool empty_ok = empty.wave == plain.wave;
  printf("  full depth, IN 2 empty (steady +3.33 V): identical to no duck  %s\n",
         empty_ok ? "OK" : "FAIL");
  all = all && empty_ok;
  // Full depth, one kick: the bass drops at once.
  DuckRun full = RunDuck(65535, 32768, true, KickAt100ms, len);
  size_t a = hit + 93, b = hit + 249;  // 3 .. 8 ms after the kick
  double drop = 20 * log10(RmsOf(full.wave, a, b) / RmsOf(plain.wave, a, b));
  DuckRun half = RunDuck(46341, 32768, true, KickAt100ms, len);  // knob at 71 %
  double half_drop = 20 * log10(RmsOf(half.wave, a, b) / RmsOf(plain.wave, a, b));
  double before = 20 * log10(RmsOf(full.wave, 1000, 3000) / RmsOf(plain.wave, 1000, 3000));
  bool depth_ok = drop < -20 && half_drop > -8 && half_drop < -4 && fabs(before) < 0.01;
  printf("  6 V kick, 3-8 ms in: full depth %.1f dB, 71 %% depth %.1f dB, before it %.2f dB  %s\n",
         drop, half_drop, before, depth_ok ? "OK" : "FAIL");
  all = all && depth_ok;
  // Rest level: a kick with no note playing leaves the VCA word at the lift.
  DuckRun rest = RunDuck(65535, 32768, false, KickAt100ms, len);
  bool rest_ok = true;
  for (int32_t w : rest.wave) rest_ok = rest_ok && w == 0;
  printf("  kick with no note: OUT 1 stays at the rest level  %s\n",
         rest_ok ? "OK" : "FAIL");
  all = all && rest_ok;
  // Release counts from the hit: with a long kick (still ringing), the bass
  // is fully back (duck under 1 %) after the Release time. Curve (1 - t/T)^2
  // is under 1 % from 0.9 T, plus the 8 ms glide (about 18 ms more).
  const uint16_t knobs[3] = { 0, 32768, 65535 };
  const double times[3] = { 40.0, 40.0 * sqrt(20.0), 800.0 };
  for (int k = 0; k < 3; ++k) {
    DuckRun r = RunDuck(65535, knobs[k], false, KickAt100ms, len);
    int i = hit + 200;
    while (i < len && r.level[i] >= 328) ++i;
    double ms = (i - hit) * 1000.0 / kFirmwareRate;
    double expected = 0.9 * times[k] + 18.0;
    bool ok = fabs(ms - expected) < expected * 0.15;
    printf("  release knob %3d %%, long kick: bass fully back %.0f ms after the hit (expected %.0f)  %s\n",
           knobs[k] * 100 / 65535, ms, expected, ok ? "OK" : "FAIL");
    all = all && ok;
  }
  // A short kick: the bass comes back with the kick, long before Release.
  DuckRun shortk = RunDuck(65535, 65535, false, ShortKick, len);
  int i = hit + 100;
  while (i < len && shortk.level[i] >= 3277) ++i;
  double short_ms = (i - hit) * 1000.0 / kFirmwareRate;
  bool short_ok = short_ms < 150;
  printf("  20 ms kick, release fully CW (800 ms): bass back (under 10 %%) %.0f ms after the hit  %s\n",
         short_ms, short_ok ? "OK" : "FAIL");
  all = all && short_ok;
  // Smooth: after its lowest point the bass only comes back up, without
  // steps (the duck never deepens again by more than 0.2 %).
  int32_t worst_rise = 0;
  for (int k = 0; k < 2; ++k) {
    DuckRun r = RunDuck(65535, 65535, false, k ? KickAt100ms : ShortKick, len);
    int top = hit;
    for (int j = hit; j < hit + 1000; ++j) if (r.level[j] > r.level[top]) top = j;
    for (int j = top + 1; j < len; ++j) worst_rise = std::max(worst_rise, r.level[j] - r.level[j - 1]);
  }
  bool smooth_ok = worst_rise <= 66;
  printf("  after the deepest point the duck only lets go: worst step back down %.2f %%  %s\n",
         worst_rise * 100.0 / 32767, smooth_ok ? "OK" : "FAIL");
  all = all && smooth_ok;
  // A kick on every beat: each one ducks fully, and the bass is back before
  // the next (release at noon).
  DuckRun beats = RunDuck(65535, 32768, false, KickOnBeats, 3 * 15288);
  bool beats_ok = true;
  for (int k = 0; k < 3; ++k) {
    int32_t deep = 0;
    for (int j = k * 15288; j < k * 15288 + 600; ++j) deep = std::max(deep, beats.level[j]);
    beats_ok = beats_ok && deep > 31000 && beats.level[k * 15288 + 15200] < 328;
  }
  printf("  kick on every beat at 122 BPM: each one ducks fully, bass back before the next  %s\n",
         beats_ok ? "OK" : "FAIL");
  all = all && beats_ok;
  // One-sample steps of the ducked wave stay small: no click from the duck.
  int32_t worst = 0, worst_plain = 0;
  for (size_t j = hit; j < hit + 1555; ++j) {
    worst = std::max(worst, abs(full.wave[j] - full.wave[j - 1]));
    worst_plain = std::max(worst_plain, abs(plain.wave[j] - plain.wave[j - 1]));
  }
  bool click_ok = worst <= worst_plain;
  printf("  largest one-sample step while ducking %d (bass alone %d)  %s\n",
         worst, worst_plain, click_ok ? "OK" : "FAIL");
  return all && click_ok;
}

// The roller with a kick on every beat, ducked and not, kick mixed in (as on
// a mixer), for listening: build/tomline_duck.wav, tomline_duck_off.wav.
void WriteDuckWavs(const Patch& base, const std::vector<Step>& roller) {
  for (int on = 0; on < 2; ++on) {
    Patch patch = base;
    patch.duck_layer[0] = on ? 52000 : 0;
    g_sidechain = KickOnBeats;
    Result r = Render(patch, roller, kKickBpm, 4);
    g_sidechain = NULL;
    // AC-couple OUT 1 (3 Hz), then add the kick.
    double x1 = r.raw[0], y1 = 0, k = 1.0 - 2 * M_PI * 3.0 / kHostRate;
    double beat = 60.0 / kKickBpm;
    std::vector<float> mix(r.raw.size());
    for (size_t i = 0; i < r.raw.size(); ++i) {
      double y = r.raw[i] - x1 + k * y1;
      x1 = r.raw[i]; y1 = y;
      double t = i / kHostRate;
      mix[i] = static_cast<float>(y + KickVolts(fmod(t, beat)) * 0.6);
    }
    WriteWav(on ? "build/tomline_duck.wav" : "build/tomline_duck_off.wav", mix, false);
  }
}

int main() {
  int failures = 0;
  Patch bt = { ENGINE_BT, 2, { 9146, 40632, 32768, 19661 },
               { 0, 16384, 0, 0 } };
  Patch lt = bt;
  lt.engine = ENGINE_LT;

  // "Roller" pattern from the browser page (V/oct in semitones).
  std::vector<Step> roller = {
    {12}, {-1}, {12}, {24}, {-1}, {15}, {12}, {10},
    {12}, {-1}, {12}, {19}, {17}, {15}, {-1}, {10}};

  struct { const char* name; Patch patch; } cases[] = {
    { "build/tomline_bt.wav", bt },
    { "build/tomline_lt.wav", lt },
  };
  for (auto& c : cases) {
    Result r = Render(c.patch, roller, 122.0, 4);
    WriteWav(c.name, r.raw, true);
    bool rest_ok = fabs(r.rest_v - 3.33) < 0.01;
    printf("%-28s OUT 1 min %.3f V  max %.2f V  after the tail %.4f V  %s\n",
           c.name, r.min_v, r.max_v, r.rest_v, rest_ok ? "OK" : "FAIL");
    failures += !rest_ok;
  }

  printf("\nAttack jump (average OUT 1, 100 ms after a hit minus 100 ms before):\n");
  Patch quiet = bt;
  quiet.snap = 0;
  quiet.main_layer[1] = 0;      // tone fully closed
  quiet.main_layer[3] = 0;      // no distortion
  quiet.second_layer[0] = 0;    // no crush
  struct { const char* name; Patch patch; } hits[] = {
    { "BT default", bt }, { "LT default", lt }, { "BT tone closed, snap off", quiet } };
  for (auto& h : hits) {
    double jump = AttackJumpVolts(h.patch);
    bool ok = fabs(jump) < 0.25;  // the old moving lift: about 2.4 V
    printf("  %-26s %+.3f V  %s\n", h.name, jump, ok ? "OK" : "FAIL (click on every hit)");
    failures += !ok;
  }

  printf("\nPitch at the default V/oct scale (0 V = C2), BT, tune centered:\n");
  int notes[] = { 0, 12, 24, 36 };
  for (int note : notes) {
    double expected = 65.40639133 * pow(2.0, note / 12.0);
    double measured = MeasurePitch(bt, note);
    printf("  %d V  expected %7.2f Hz  measured %7.2f Hz  error %+.1f cents\n",
           note / 12, expected, measured, 1200.0 * log2(measured / expected));
  }

  double tune_error = TuneRangeError();
  bool tune_ok = tune_error < 0.01;
  printf("\nTune: fully left C1, noon C2, fully right C3: worst error %.4f st  %s\n",
         tune_error, tune_ok ? "OK" : "FAIL");
  failures += !tune_ok;

  double glide = GlideErrorCents();
  bool glide_ok = fabs(glide) < 0.5;
  printf("Glide C2 -> C3, 1.5 s later: %+.2f cents  %s\n", glide,
         glide_ok ? "OK" : "FAIL (glide stops short of the note)");
  failures += !glide_ok;

  bool lfo_params_ok = LfoParametersOk();
  printf("LFO knobs (0.05 / 1 / 20 Hz, depth off / 12 st, wave shapes): %s\n",
         lfo_params_ok ? "OK" : "FAIL");
  failures += !lfo_params_ok;
  for (int free_run = 0; free_run < 2; ++free_run) {
    for (uint8_t shape = 0; shape < LFO_SHAPE_LAST; ++shape) {
      double measured;
      double error = LfoPitchError(shape, free_run != 0, &measured);
      bool ok = error < 0.1;
      printf("LFO %-8s %-8s at full depth, 0.2-1.2 s: %+.2f st from C2 (error %.3f st)  %s\n",
             shape == LFO_RAMP_UP ? "ramp up" : "triangle",
             free_run ? "free" : "restart", measured, error, ok ? "OK" : "FAIL");
      failures += !ok;
    }
  }
  bool lfo_restart_ok = LfoRestartOk();
  printf("LFO restart: every trigger restarts it at mid-wave, free run leaves it: %s\n",
         lfo_restart_ok ? "OK" : "FAIL");
  failures += !lfo_restart_ok;
  bool sweep_start, sweep_off;
  double sweep_cents = SweepTailCents(&sweep_start, &sweep_off);
  bool sweep_ok = sweep_start && sweep_off && fabs(sweep_cents) < 1.0;
  printf("Pitch sweep: off = none, full starts higher, 0.2-1.2 s later %+.2f cents from C2  %s\n",
         sweep_cents, sweep_ok ? "OK" : "FAIL");
  failures += !sweep_ok;
  bool lfo_off_ok = LfoOffIsSilent(roller);
  printf("LFO depth at zero, speed turned: sound unchanged  %s\n",
         lfo_off_ok ? "OK" : "FAIL");
  failures += !lfo_off_ok;

  for (uint8_t engine = 0; engine < 2; ++engine) {
    failures += !DistortionOk(engine);
    failures += !DistortionLevelOk(engine);
  }
  for (uint8_t engine = 0; engine < 2; ++engine) {
    failures += !WaveOk(engine);
    failures += !WaveLevelOk(engine);
    failures += !CrushOk(engine);
  }
  failures += !WaveCvOk();

  printf("\nTone fully closed vs fully open, RMS over a note (must be >= -6 dB):\n");
  for (int e = 0; e < 2; ++e) {
    printf("  %s", e ? "LT" : "BT");
    for (int v = 0; v <= 4; ++v) {
      double db = ToneClosedDb(e ? lt : bt, v * 12);
      bool ok = db >= -6.0;
      printf("  %d V %+.1f dB%s", v, db, ok ? "" : " FAIL");
      failures += !ok;
    }
    printf("\n");
  }

  bool snap_ok = SnapOffOk();
  printf("Snap off: no click noise, no pitch blip: %s\n", snap_ok ? "OK" : "FAIL");
  failures += !snap_ok;
  printf("Retrigger click, largest one-sample jump in the 1 ms after a note:\n");
  const uint16_t decays[3] = { 32768, 49152, 65535 };
  for (uint8_t engine = 0; engine < ENGINE_LAST; ++engine) {
    for (uint16_t decay : decays) {
      failures += !RetriggerOk(engine, decay);
    }
  }

  printf("Knob jump click, largest one-sample jump in the 100 ms after it:\n");
  struct { int knob; const char* name; } jumps[] = {
    { 0, "decay" }, { 1, "tone" }, { 2, "tune" }, { 3, "distortion" },
    { 4, "crush" }, { 6, "wave" }, { 7, "glide" },
    { 9, "LFO depth" } };
  for (uint8_t engine = 0; engine < ENGINE_LAST; ++engine) {
    for (auto& j : jumps) {
      failures += !KnobJumpOk(engine, j.knob, j.name, 0, 65535);
      failures += !KnobJumpOk(engine, j.knob, j.name, 65535, 0);
    }
  }

  bool power_ok = PowerUpOk();
  printf("Power-up: OUT 1 at 0 V for 1 s, then a slow rise to 3.33 V: %s\n",
         power_ok ? "OK" : "FAIL");
  failures += !power_ok;

  bool mix_ok = MixScalingOk();
  printf("Sidechain duck (kick on IN 2):\n");
  failures += !DuckOk();
  WriteDuckWavs(bt, roller);
  printf("  wrote build/tomline_duck.wav (ducked) and tomline_duck_off.wav\n");

  printf("Loud output mix (82000, beyond the real 50000): %s\n",
         mix_ok ? "clips correctly  OK" : "wraps around  FAIL (32-bit overflow)");
  failures += !mix_ok;
  return failures ? 1 : 0;
}
