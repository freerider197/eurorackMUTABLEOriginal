#!/bin/bash
# UI test (real ui.cc/settings.cc with stubbed buttons, LEDs and flash), all
# sounds stability test, and a decode of a .wav with the bootloader's own
# QPSK receiver.   rings/tools/sim/run_v4_tests.sh [update.wav]
set -e
cd "$(dirname "$0")/../../.."
OUT=build/sim; mkdir -p $OUT/stubs/rings
cp -r rings/tools/sim/ui_stubs/* $OUT/stubs/
# Host longs are 64-bit: ui.h's std::min(50L, int32_t) needs a cast there.
sed 's/std::min(50L, strumming_flag_interval_ >> 2)/std::min<int32_t>(50, strumming_flag_interval_ >> 2)/' rings/ui.h > $OUT/stubs/rings/ui.h
DSP="rings/dsp/part.cc rings/dsp/string_synth_part.cc rings/dsp/fm_voice.cc rings/dsp/resonator.cc rings/dsp/string.cc rings/dsp/exciter.cc rings/dsp/tube.cc rings/resources.cc stmlib/dsp/units.cc stmlib/utils/random.cc"
g++ -O1 -DTEST -I$OUT/stubs -I. rings/tools/sim/ui_test.cc rings/ui.cc rings/settings.cc stmlib/system/system_clock.cc $DSP -o $OUT/ui_test
g++ -O2 -DTEST -I. rings/tools/sim/all_sounds_test.cc $DSP -o $OUT/all_sounds_test
g++ -O2 -DTEST -I. rings/tools/sim/bootloader_decode.cc stm_audio_bootloader/qpsk/demodulator.cc stm_audio_bootloader/qpsk/packet_decoder.cc -o $OUT/bootloader_decode
$OUT/ui_test
$OUT/all_sounds_test
[ -n "$1" ] && $OUT/bootloader_decode "$1" $OUT/decoded.bin
true
