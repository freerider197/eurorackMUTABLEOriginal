#!/usr/bin/env python3
#
# Python 3 port of stm_audio_bootloader/qpsk/encoder.py and
# stm_audio_bootloader/audio_stream_writer.py (Copyright 2013 Emilie Gillet,
# MIT license). Same signal, same packet format, same defaults.
#
# Usage, as in streams/makefile:
#   python3 encoder.py -s 48000 -b 12000 -c 6000 -p 256 build/tomline/tomline.bin

import logging
import optparse
import struct
import sys
import zlib

import numpy


class Scrambler(object):

  def __init__(self, seed=0):
    self._state = seed

  def scramble(self, data):
    out = bytearray(data)
    for index in range(len(out)):
      out[index] ^= (self._state >> 24) & 0xff
      self._state = (self._state * 1664525 + 1013904223) & 0xffffffff
    return bytes(out)


class QpskEncoder(object):

  def __init__(
      self,
      sample_rate=48000,
      carrier_frequency=2000,
      bit_rate=8000,
      packet_size=256,
      scramble=False):
    period = sample_rate // carrier_frequency
    symbol_time = sample_rate // (bit_rate // 2)

    assert (symbol_time % period == 0) or (period % symbol_time == 0)
    assert (sample_rate % carrier_frequency) == 0
    assert (sample_rate % bit_rate) == 0

    self._sr = sample_rate
    self._br = bit_rate
    self._carrier_frequency = carrier_frequency
    self._sample_index = 0
    self._packet_size = packet_size
    self._scrambler = Scrambler(0) if scramble else None

  @staticmethod
  def _upsample(x, factor):
    return numpy.tile(x.reshape(len(x), 1), (1, factor)).ravel()

  def _encode_qpsk(self, symbol_stream):
    ratio = self._sr // self._br * 2
    symbol_stream = numpy.array(symbol_stream, dtype=int)
    bitstream_even = 2 * self._upsample(symbol_stream % 2, ratio) - 1
    bitstream_odd = 2 * self._upsample(symbol_stream // 2, ratio) - 1
    return bitstream_even / numpy.sqrt(2.0), bitstream_odd / numpy.sqrt(2.0)

  def _modulate(self, q_mod, i_mod):
    num_samples = len(q_mod)
    t = (numpy.arange(0.0, num_samples) + self._sample_index) / self._sr
    self._sample_index += num_samples
    phase = 2 * numpy.pi * self._carrier_frequency * t
    return (q_mod * numpy.sin(phase) + i_mod * numpy.cos(phase))

  def _encode(self, symbol_stream):
    return self._modulate(*self._encode_qpsk(symbol_stream))

  def _code_blank(self, duration):
    num_zeros = int(duration * self._br / 8) * 4
    symbol_stream = numpy.zeros(num_zeros, dtype=int)
    return self._encode(symbol_stream)

  def _code_packet(self, data):
    assert len(data) <= self._packet_size
    if len(data) != self._packet_size:
      data = data + b'\xff' * (self._packet_size - len(data))

    if self._scrambler:
      data = self._scrambler.scramble(data)

    crc = zlib.crc32(data) & 0xffffffff

    # 16x 0 for the PLL ; 8x 21 for the edge detector ; 8x 3030 for syncing
    preamble = [0] * 8 + [0x99] * 4 + [0xcc] * 4
    crc_bytes = [crc >> 24, (crc >> 16) & 0xff, (crc >> 8) & 0xff, crc & 0xff]
    all_bytes = preamble + list(data) + crc_bytes

    symbol_stream = []
    for byte in all_bytes:
      symbol_stream.append((byte >> 6) & 0x3)
      symbol_stream.append((byte >> 4) & 0x3)
      symbol_stream.append((byte >> 2) & 0x3)
      symbol_stream.append((byte >> 0) & 0x3)

    return self._encode(symbol_stream)

  def code_intro(self):
    yield numpy.zeros(1 * self._sr)
    yield self._code_blank(1.0)

  def code_outro(self, duration=1.0):
    yield self._code_blank(duration)

  def code(self, data, page_size=1024, blank_duration=0.06):
    if len(data) % page_size != 0:
      tail = page_size - (len(data) % page_size)
      data += b'\xff' * tail

    offset = 0
    remaining_bytes = len(data)
    num_packets_written = 0
    while remaining_bytes:
      size = min(remaining_bytes, self._packet_size)
      yield self._code_packet(data[offset:offset + size])
      num_packets_written += 1
      if num_packets_written == page_size // self._packet_size:
        yield self._code_blank(blank_duration)
        num_packets_written = 0
      remaining_bytes -= size
      offset += size


class AudioStreamWriter(object):

  def __init__(self, file_name, sample_rate, bitdepth=16, num_channels=1):
    self._sample_rate = sample_rate
    self._bitdepth = bitdepth
    self._num_channels = num_channels
    self._f = open(file_name, 'wb')
    self._num_bytes = 0
    self._write_header(False)

  @staticmethod
  def quantize(signal, bitdepth):
    scaled_signal = numpy.array(signal, dtype=float)
    if numpy.abs(scaled_signal).max() > 1.0:
      logging.warning('Some samples will be clipped.')
      scaled_signal = numpy.clip(scaled_signal, -1.0, 1.0)
    scale = (1 << (bitdepth - 1)) - 1
    return numpy.array(scaled_signal * scale, dtype='<i%d' % (bitdepth // 8))

  def _write_header(self, restore_position):
    f = self._f
    total_size = 4 + 8 + 16 + 8 + self._num_bytes
    current_position = f.tell()
    f.seek(0)
    f.write(b'RIFF')
    f.write(struct.pack('<L', total_size))
    f.write(b'WAVEfmt ')
    bitrate = self._sample_rate * self._num_channels * (self._bitdepth // 8)
    bits_per_sample = self._num_channels * (self._bitdepth // 8)
    f.write(struct.pack(
        '<LHHLLHH', 16, 1, self._num_channels, self._sample_rate, bitrate,
        bits_per_sample, self._bitdepth))
    f.write(b'data')
    f.write(struct.pack('<L', self._num_bytes))
    if restore_position:
      f.seek(current_position)

  def append(self, signal):
    scaled_signal = self.quantize(signal, self._bitdepth)
    scaled_signal.tofile(self._f)
    self._num_bytes += scaled_signal.nbytes
    self._write_header(True)

  def close(self):
    self._f.close()


PAGE_SIZE = {'stm32f1': 1024, 'stm32f3': 2048}
PAUSE = {'stm32f1': 0.06, 'stm32f3': 0.15}


def main():
  parser = optparse.OptionParser()
  parser.add_option('-k', '--scramble', dest='scramble', action='store_true',
                    default=False, help='Randomize data stream')
  parser.add_option('-s', '--sample_rate', dest='sample_rate', type='int',
                    default=48000, help='Sample rate in Hz')
  parser.add_option('-c', '--carrier_frequency', dest='carrier_frequency',
                    type='int', default=6000, help='Carrier frequency in Hz')
  parser.add_option('-b', '--baud_rate', dest='baud_rate', type='int',
                    default=12000, help='Baudrate in bps')
  parser.add_option('-p', '--packet_size', dest='packet_size', type='int',
                    default=256, help='Packet size in bytes')
  parser.add_option('-o', '--output_file', dest='output_file', default=None,
                    help='Write output file to FILE', metavar='FILE')
  parser.add_option('-t', '--target', dest='target', default='stm32f1',
                    help='Set page size and erase time for TARGET',
                    metavar='TARGET')
  options, args = parser.parse_args()

  if len(args) != 1:
    logging.fatal('Specify one, and only one firmware .bin file!')
    sys.exit(1)
  if options.target not in PAGE_SIZE:
    logging.fatal('Unknown target: %s' % options.target)
    sys.exit(2)

  with open(args[0], 'rb') as f:
    data = f.read()

  output_file = options.output_file
  if not output_file:
    output_file = args[0].replace('.bin', '.wav') if '.bin' in args[0] \
        else args[0] + '.wav'

  encoder = QpskEncoder(options.sample_rate, options.carrier_frequency,
                        options.baud_rate, options.packet_size,
                        options.scramble)
  writer = AudioStreamWriter(output_file, options.sample_rate, 16, 1)
  for block in encoder.code_intro():
    writer.append(block)
  for block in encoder.code(data, PAGE_SIZE[options.target],
                            PAUSE[options.target]):
    if len(block):
      writer.append(block)
  for block in encoder.code_outro(1.0):
    writer.append(block)
  writer.close()


if __name__ == '__main__':
  main()
