#!/usr/bin/env python3
# Python 3 port of stm_audio_bootloader/qpsk/encoder.py + audio_stream_writer.py
# (stm32f4 target only), same defaults as `make -f rings/makefile wav`.
import struct, sys, zlib
import numpy as np

SR, CARRIER, BR, PACKET = 48000, 6000, 12000, 256
F4_SECTORS = [0x08000000, 0x08004000, 0x08008000, 0x0800C000, 0x08010000,
              0x08020000, 0x08040000, 0x08060000, 0x08080000, 0x080A0000,
              0x080C0000, 0x080E0000]
BLOCK, START = 16384, 0x08008000

class Enc:
  def __init__(s): s.idx = 0
  def encode(s, symbols):
    ratio = SR // BR * 2
    sym = np.array(symbols)
    up = lambda x: np.tile(x.reshape(len(x), 1), (1, ratio)).ravel()
    even = (2 * up(sym % 2) - 1) / np.sqrt(2.0)
    odd = (2 * up(sym // 2) - 1) / np.sqrt(2.0)
    t = (np.arange(0.0, len(even)) + s.idx) / SR
    s.idx += len(even)
    ph = 2 * np.pi * CARRIER * t
    return even * np.sin(ph) + odd * np.cos(ph)
  def blank(s, dur):
    return s.encode(np.zeros(int(dur * BR / 8) * 4, dtype=int))
  def packet(s, data):
    data = data + b'\xff' * (PACKET - len(data))
    crc = zlib.crc32(data) & 0xffffffff
    b = [0] * 8 + [0x99] * 4 + [0xcc] * 4 + list(data) + \
        [crc >> 24, (crc >> 16) & 0xff, (crc >> 8) & 0xff, crc & 0xff]
    return s.encode([(x >> k) & 3 for x in b for k in (6, 4, 2, 0)])
  def code(s, data, page, pause):
    if len(data) % page: data += b'\xff' * (page - len(data) % page)
    n = 0
    for off in range(0, len(data), PACKET):
      yield s.packet(data[off:off + PACKET]); n += 1
      if n == page // PACKET: yield s.blank(pause); n = 0

def main(src, dst):
  data = open(src, 'rb').read()
  e = Enc(); blocks = [np.zeros(SR), e.blank(1.0)]
  for x in range(0, len(data), BLOCK):
    pause = 3.5 if START + x in F4_SECTORS else 0.2
    blocks += list(e.code(data[x:x + BLOCK], BLOCK, pause))
  blocks.append(e.blank(5.0))
  out = b''.join(np.array(np.clip(b, -1, 1) * 32767, dtype='<i2').tobytes() for b in blocks)
  with open(dst, 'wb') as f:
    f.write(b'RIFF' + struct.pack('<L', 36 + len(out)) + b'WAVEfmt ' +
            struct.pack('<LHHLLHH', 16, 1, 1, SR, SR * 2, 2, 16) +
            b'data' + struct.pack('<L', len(out)) + out)

if __name__ == '__main__':
  main(sys.argv[1], sys.argv[2])
