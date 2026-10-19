// Runs the Rings bootloader's QPSK receive path over a .wav (offline).
#include <cstdio>
#include <cstring>
#include <vector>
#include "stm_audio_bootloader/qpsk/demodulator.h"
#include "stm_audio_bootloader/qpsk/packet_decoder.h"
using namespace stm_audio_bootloader;
const double kSampleRate = 48000.0, kModulationRate = 6000.0, kBitRate = 12000;
const int kBlockSize = 16384, kPacketsPerBlock = kBlockSize / kPacketSize;
int main(int argc, char** argv) {
  FILE* f = fopen(argv[1], "rb"); fseek(f, 44, SEEK_SET);
  std::vector<int16_t> s; int16_t x; while (fread(&x, 2, 1, f)) s.push_back(x); fclose(f);
  Demodulator demodulator; PacketDecoder decoder;
  decoder.Init(20000);
  demodulator.Init(kModulationRate / kSampleRate * 4294967296.0, kSampleRate / kModulationRate, 2.0 * kSampleRate / kBitRate);
  demodulator.SyncCarrier(true); decoder.Reset();
  std::vector<uint8_t> out, block(kBlockSize); int packets = 0; size_t discard = 8000;
  for (size_t i = 0; i < s.size(); ++i) {
    if (discard) { --discard; continue; }
    demodulator.PushSample(2048 + (s[i] >> 4));
    if (demodulator.state() == DEMODULATOR_STATE_OVERFLOW) { printf("OVERFLOW at %zu\n", i); return 1; }
    demodulator.ProcessAtLeast(0);
    while (demodulator.available()) {
      PacketDecoderState st = decoder.ProcessSymbol(demodulator.NextSymbol());
      if (st == PACKET_DECODER_STATE_OK) {
        memcpy(&block[(packets % kPacketsPerBlock) * kPacketSize], decoder.packet_data(), kPacketSize);
        ++packets;
        if (packets % kPacketsPerBlock == 0) {
          out.insert(out.end(), block.begin(), block.end());
          // ProgramPage(): erasing a sector takes ~1s (16KB pages ~20ms), during
          // which received audio is dropped by SyncCarrier's flush.
          uint32_t address = 0x08008000 + out.size() - kBlockSize;
          bool sector = address == 0x08008000 || address == 0x0800C000 || address == 0x08010000 || (address >= 0x08020000 && (address & 0x1FFFF) == 0);
          i += sector ? 48000 : 1000;
          decoder.Reset(); demodulator.SyncCarrier(false);
        }
        else { decoder.Reset(); demodulator.SyncDecision(); }
      } else if (st == PACKET_DECODER_STATE_ERROR_SYNC || st == PACKET_DECODER_STATE_ERROR_CRC) {
        printf("ERROR %s after %d packets (sample %zu)\n", st == PACKET_DECODER_STATE_ERROR_CRC ? "CRC" : "SYNC", packets, i); return 1;
      } else if (st == PACKET_DECODER_STATE_END_OF_TRANSMISSION) {
        printf("END OF TRANSMISSION: %d packets, %zu bytes\n", packets, out.size());
        FILE* o = fopen(argv[2], "wb"); fwrite(out.data(), 1, out.size(), o); fclose(o); return 0;
      }
    }
  }
  printf("ran out of samples: %d packets, no end-of-transmission\n", packets); return 2;
}
