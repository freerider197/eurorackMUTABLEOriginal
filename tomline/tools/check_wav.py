#!/usr/bin/env python3
"""Decodes a Streams/STM32F1 audio-bootloader WAV (48 kHz, 6 kHz carrier,
12 kbps QPSK, 256-byte packets) and checks every packet CRC.

  python3 check_wav.py firmware.wav [firmware.bin]

With a .bin, also checks that the decoded bytes equal the .bin."""
import struct, sys, zlib
import numpy

def read_wav(path):
    with open(path, 'rb') as f:
        d = f.read()
    pos = 12
    fmt = None
    while pos < len(d):
        cid, size = d[pos:pos+4], struct.unpack('<L', d[pos+4:pos+8])[0]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHLLHH', d[pos+8:pos+24])
        if cid == b'data':
            pcm = numpy.frombuffer(d[pos+8:pos+8+size], dtype='<i2')
            return fmt, pcm.astype(float)
        pos += 8 + size + (size & 1)

def demodulate(x, sr=48000, carrier=6000, samples_per_symbol=8):
    n = len(x) // samples_per_symbol * samples_per_symbol
    t = numpy.arange(n) / sr
    s = numpy.sin(2 * numpy.pi * carrier * t) * x[:n]
    c = numpy.cos(2 * numpy.pi * carrier * t) * x[:n]
    q = s.reshape(-1, samples_per_symbol).sum(axis=1)
    i = c.reshape(-1, samples_per_symbol).sum(axis=1)
    return (q > 0).astype(int) + 2 * (i > 0).astype(int)

def packets(symbols):
    sync = []
    for b in [0x99] * 4 + [0xcc] * 4:
        sync += [(b >> 6) & 3, (b >> 4) & 3, (b >> 2) & 3, b & 3]
    sync = numpy.array(sync)
    k, n, out = 0, len(sync), []
    while k + n + 260 * 4 <= len(symbols):
        if symbols[k] == sync[0] and numpy.array_equal(symbols[k:k+n], sync):
            body = symbols[k+n:k+n+260*4].reshape(-1, 4)
            by = bytes(int(a << 6 | b << 4 | c << 2 | d) for a, b, c, d in body)
            data, crc = by[:256], struct.unpack('>L', by[256:])[0]
            out.append((data, (zlib.crc32(data) & 0xffffffff) == crc))
            k += n + 260 * 4
        else:
            k += 1
    return out

fmt, x = read_wav(sys.argv[1])
ps = packets(demodulate(x))
good = sum(ok for _, ok in ps)
print('%s: %d Hz, %d packets, %d with a valid CRC' % (sys.argv[1], fmt[2], len(ps), good))
if len(sys.argv) > 2:
    with open(sys.argv[2], 'rb') as f:
        binary = f.read()
    decoded = b''.join(d for d, _ in ps)
    print('matches %s: %s' % (sys.argv[2], decoded[:len(binary)] == binary and
          set(decoded[len(binary):]) <= {0xff}))
