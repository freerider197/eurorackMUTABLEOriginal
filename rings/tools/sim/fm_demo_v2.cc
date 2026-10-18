// Demo render: drone at 110Hz, sweeping POSITION from noon to full left
// (DX7 self-feedback), back, then to full right (classic), at BRIGHTNESS 0
// and 0.6; then plucks with BRIGHTNESS closing from 1 to 0.
//   fm_demo_v2 out.wav
#include "rings/dsp/fm_voice.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
using namespace rings;
static int16_t buf[48000 * 40]; static int n = 0;
static void emit(FMVoice& v, int blocks) {
  float in[24] = { 0 }, out[24], aux[24];
  for (int b = 0; b < blocks; ++b) {
    v.Process(in, out, aux, 24);
    for (int i = 0; i < 24; ++i) {
      float x = out[i] * 0.7f * 0.8f; x = x > 1 ? 1 : x < -1 ? -1 : x;
      buf[n++] = (int16_t)(x * 32767);
    }
  }
}
int main(int argc, char** argv) {
  FMVoice v; v.Init();
  v.set_frequency(110.f / kSampleRate); v.set_ratio(0.5f); v.set_damping(1.0f);
  float brights[2] = { 0.0f, 0.6f };
  for (int k = 0; k < 2; ++k) {
    v.set_brightness(brights[k]);
    for (int s = 0; s < 3; ++s) {  // noon->left, left->noon, noon->right
      for (int j = 0; j < 100; ++j) {  // 3s per sweep
        float t = j / 99.0f;
        float pos = s == 0 ? 0.5f - 0.5f * t : s == 1 ? 0.5f * t : 0.5f + 0.5f * t;
        v.set_feedback_amount(pos); emit(v, 60);
      }
    }
  }
  v.set_feedback_amount(0.5f); v.set_damping(0.45f);
  for (int k = 0; k <= 8; ++k) {
    v.set_brightness(1.0f - k / 8.0f); v.TriggerInternalEnvelope(); emit(v, 1000);
  }
  FILE* f = fopen(argv[1], "wb");
  uint32_t bytes = n * 2, sr = 48000, br = 96000, fmt = 16, riff = 36 + bytes;
  uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
  fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f);
  fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
  fwrite(buf, 2, n, f); fclose(f);
}
