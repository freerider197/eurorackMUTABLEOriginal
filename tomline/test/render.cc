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
  uint16_t main_layer[4];    // decay, tone, tune, sweep
  uint16_t second_layer[4];  // noise, +1 oct, sweep time, glide
  uint16_t lfo_layer[4];     // LFO speed, LFO depth, unused, unused
  uint8_t lfo_shape;         // LFO_TRIANGLE / LFO_RAMP_UP
  bool lfo_free;             // false = LFO restarts on each trigger
};

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
                         patch.lfo_free);
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
    if (control && control(n, &patch)) {
      ComputeVoiceParameters(patch.engine, patch.snap, patch.main_layer,
                             patch.second_layer, patch.lfo_layer, patch.lfo_shape, &p);
    }
    int32_t semitones = counts * kVoctK >> 8;
    pitch += (semitones - pitch) >> 5;
    voice.Process(p, trigger, pitch, &gain, &frequency);
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
  Result r = Render(patch, steps, 30.0, 1);
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

// Overflow check: the output mix can reach about 82000 with noise at 100 %
// on a hard hit. Scaling it must clip at the top, never wrap to the bottom.
bool MixScalingOk() {
  return Voice::ScaleMix(82000) == 32767 && Voice::ScaleMix(70000) == 32767 &&
         Voice::ScaleMix(-82000) == -32767 && Voice::ScaleMix(1000) == 950;
}

// Tune range: SHAPE 2 fully left = -12 st (C1 at 0 V), noon = C2, fully
// right = +12 st (C3). Returns the worst error in semitones.
double TuneRangeError() {
  const uint16_t knob[3] = { 0, 32768, 65535 };
  const double expected[3] = { 0.0, 12.0, 24.0 };  // semitones above C1
  double worst = 0;
  for (int i = 0; i < 3; ++i) {
    Patch patch = { ENGINE_BT, 1, { 29491, 40632, knob[i], 19661 },
                    { 9830, 16384, 22938, 0 } };
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
  Patch patch = { ENGINE_BT, 1, { 0, 0, 32768, 0 }, { 0, 0, 0, 0 },
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
    uint16_t main_layer[4] = { 29491, 40632, 32768, 19661 };
    uint16_t second[4] = { 9830, 16384, 22938, 0 };
    VoiceParameters p;
    ComputeVoiceParameters(ENGINE_BT, 1, main_layer, second, lfo, LFO_TRIANGLE, &p);
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
  uint16_t main_layer[4] = { 29491, 40632, 32768, 19661 };
  uint16_t second[4] = { 9830, 16384, 22938, 0 };
  uint16_t lfo[4] = { 50000, 40000, 0, 0 };
  bool ok = true;
  for (uint8_t shape = 0; shape < LFO_SHAPE_LAST; ++shape) {
    for (int free_run = 0; free_run < 2; ++free_run) {
      VoiceParameters p;
      ComputeVoiceParameters(ENGINE_BT, 1, main_layer, second, lfo, shape, &p,
                             free_run != 0);
      Voice voice;
      voice.Init();
      uint16_t gain, frequency;
      for (int hit = 0; hit < 3; ++hit) {
        for (int n = 0; n < 10007 * (hit + 1); ++n) {
          voice.Process(p, false, 0, &gain, &frequency);
        }
        uint32_t before = voice.lfo_phase();
        voice.Process(p, true, 0, &gain, &frequency);
        uint32_t expected = (free_run ? before : Voice::LfoStart(shape)) +
                            static_cast<uint32_t>(p.lfo_increment);
        ok = ok && voice.lfo_phase() == expected;
      }
      ok = ok && Voice::LfoWave(Voice::LfoStart(shape), shape) == 0;
    }
  }
  return ok;
}

// Depth at zero: the speed knob must not change the sound at all.
bool LfoOffIsSilent(const std::vector<Step>& steps) {
  Patch slow = { ENGINE_BT, 1, { 29491, 40632, 32768, 19661 },
                 { 9830, 16384, 22938, 0 }, { 0, 0, 0, 0 }, LFO_TRIANGLE };
  Patch fast = slow;
  fast.lfo_layer[0] = 65535;
  fast.lfo_shape = LFO_RAMP_UP;
  return Render(slow, steps, 122.0, 1).raw ==
         Render(fast, steps, 122.0, 1).raw;
}

// Pitch envelope (MOD 2 amount, sweep time): on every hit the pitch starts on
// the root, rises to root + amount in about 1 ms, then decays back onto the
// root. A retrigger in the middle of the sweep starts again from the root.
// Reads the voice's own sweep offset, sample by sample, with snap off.
bool PitchEnvelopeOk(uint8_t engine) {
  Patch patch = { engine, 0, { 29491, 40632, 32768, 65535 },
                  { 9830, 16384, 22938, 0 } };
  VoiceParameters p;
  ComputeVoiceParameters(patch.engine, patch.snap, patch.main_layer,
                         patch.second_layer, patch.lfo_layer, patch.lfo_shape, &p);
  double amount = p.sweep_depth / 65536.0;
  Voice voice;
  voice.Init();
  uint16_t gain, frequency;
  const int retrigger = static_cast<int>(0.010 * kFirmwareRate);  // 10 ms
  const int length = static_cast<int>(3.0 * kFirmwareRate);
  double at_hit = 0, peak = 0, peak_ms = 0, at_retrigger = 0, peak2 = 0;
  double settled = 0, before_retrigger = 0;
  for (int n = 0; n < retrigger + length; ++n) {
    bool trigger = n == 0 || n == retrigger;
    voice.Process(p, trigger, 12 << 16, &gain, &frequency);
    double st = voice.sweep_offset(p) / 65536.0;
    if (n == 0) at_hit = st;
    if (n < retrigger && st > peak) { peak = st; peak_ms = n * 1000.0 / kFirmwareRate; }
    if (n == retrigger - 1) before_retrigger = st;
    if (n == retrigger) at_retrigger = st;
    if (n > retrigger && st > peak2) peak2 = st;
    settled = st;
  }
  bool ok = at_hit == 0 && fabs(peak - amount) < 1e-3 && peak_ms > 0.8 &&
            peak_ms < 1.2 && at_retrigger == 0 && fabs(peak2 - amount) < 1e-3 &&
            fabs(settled * 100) < 1.0;
  printf("Pitch envelope %s (amount %.1f st): hit %+.2f st, peak %+.2f st at %.2f ms,"
         " %.2f st before retrigger, retrigger %+.2f st, peak %+.2f st,"
         " after 3 s %+.3f cents  %s\n",
         engine == ENGINE_BT ? "BT" : "LT", amount, at_hit, peak, peak_ms,
         before_retrigger, at_retrigger, peak2, settled * 100, ok ? "OK" : "FAIL");
  return ok;
}

// Tone fully closed must not silence the note: RMS of OUT 1 (AC-coupled,
// 3 Hz high-pass) over the first second of a note, Tone fully CCW against
// fully CW. Returns the ratio in dB. A 1 s rest first lets the filter
// control voltage settle from power-up.
double NoteRms(Patch patch, int note) {
  std::vector<Step> steps = { {-1}, {note} };
  Result r = Render(patch, steps, 15.0, 1);  // 1 s per step
  double x1 = 0, y1 = 0, k = 1.0 - 2 * M_PI * 3.0 / kHostRate, sum = 0;
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
// (from silence) against the worst retrigger. Also checks that from 5 ms on,
// each note's peak-to-peak swing is within 5 % of the same note played alone (no stacking).
std::vector<double> HighPass(const std::vector<float>& volts) {
  std::vector<double> out(volts.size());
  double x1 = 0, y1 = 0, r = 1.0 - 2 * M_PI * 3.0 / kHostRate;
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
  Patch patch = { engine, 1, { decay, 32768, 32768, 19660 },
                  { 9830, 16384, 22938, 0 } };
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
  bool ok = worst <= from_silence && level_ok;
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
  Patch patch = { engine, 1, { 65535, 32768, 32768, 19661 },
                  { 9830, 16384, 22938, 0 }, { 49152, 0, 0, 0 } };
  jump_knob = knob;
  jump_from = from;
  jump_to = to;
  *JumpValue(&patch) = from;
  double before = JumpLargest(patch, NULL);
  Patch end = patch;
  *JumpValue(&end) = to;
  double after = JumpLargest(end, NULL);
  double reference = fmax(before, after);
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

int main() {
  int failures = 0;
  Patch bt = { ENGINE_BT, 1, { 29491, 40632, 32768, 19661 },
               { 9830, 16384, 22938, 0 } };
  Patch lt = bt;
  lt.engine = ENGINE_LT;
  Patch bt_oct = bt;
  bt_oct.second_layer[1] = 39321;  // +1 octave at 60 %

  // "Roller" pattern from the browser page (V/oct in semitones).
  std::vector<Step> roller = {
    {12}, {-1}, {12}, {24}, {-1}, {15}, {12}, {10},
    {12}, {-1}, {12}, {19}, {17}, {15}, {-1}, {10}};

  struct { const char* name; Patch patch; } cases[] = {
    { "build/tomline_bt.wav", bt },
    { "build/tomline_lt.wav", lt },
    { "build/tomline_bt_oct.wav", bt_oct },
  };
  for (auto& c : cases) {
    Result r = Render(c.patch, roller, 122.0, 4);
    WriteWav(c.name, r.raw, true);
    printf("%-28s OUT 1 min %.3f V  max %.2f V  after the tail %.4f V\n",
           c.name, r.min_v, r.max_v, r.rest_v);
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
  bool lfo_off_ok = LfoOffIsSilent(roller);
  printf("LFO depth at zero, speed turned: sound unchanged  %s\n",
         lfo_off_ok ? "OK" : "FAIL");
  failures += !lfo_off_ok;

  for (uint8_t engine = 0; engine < 2; ++engine) {
    failures += !PitchEnvelopeOk(engine);
  }

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

  printf("Retrigger click, largest one-sample jump in the 1 ms after a note:\n");
  const uint16_t decays[3] = { 32768, 49152, 65535 };
  for (uint8_t engine = 0; engine < ENGINE_LAST; ++engine) {
    for (uint16_t decay : decays) {
      failures += !RetriggerOk(engine, decay);
    }
  }

  printf("Knob jump click, largest one-sample jump in the 100 ms after it:\n");
  struct { int knob; const char* name; } jumps[] = {
    { 0, "decay" }, { 1, "tone" }, { 2, "tune" }, { 3, "sweep" },
    { 4, "noise" }, { 5, "+1 octave" }, { 6, "sweep time" }, { 7, "glide" },
    { 9, "LFO depth" } };
  for (uint8_t engine = 0; engine < ENGINE_LAST; ++engine) {
    for (auto& j : jumps) {
      failures += !KnobJumpOk(engine, j.knob, j.name, 0, 65535);
      failures += !KnobJumpOk(engine, j.knob, j.name, 65535, 0);
    }
  }

  bool mix_ok = MixScalingOk();
  printf("Loud output mix (noise 100 %%, hard hit): %s\n",
         mix_ok ? "clips correctly  OK" : "wraps around  FAIL (32-bit overflow)");
  failures += !mix_ok;
  return failures ? 1 : 0;
}
