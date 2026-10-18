#!/bin/bash
# Desktop simulations of the FM voice (rings/dsp/fm_voice.cc), no hardware.
#   fm_loudness  - level (dB) vs DAMPING / POSITION, relative to the old drone
#   fm_feedback  - waveform change vs POSITION (dead-zone check)
#   fm_demo      - renders a DAMPING sweep of plucks to a .wav
set -e
cd "$(dirname "$0")/../../.."
OUT=build/sim; mkdir -p $OUT
SRC="rings/dsp/fm_voice.cc rings/resources.cc stmlib/dsp/units.cc"
for t in fm_loudness fm_feedback fm_demo; do
  g++ -O2 -DTEST -I. rings/tools/sim/$t.cc $SRC -o $OUT/$t
done
$OUT/fm_loudness 0.547   # 0.547 = RMS of the original drone
$OUT/fm_feedback
$OUT/fm_demo $OUT/fm_demo.wav && echo "Wrote $OUT/fm_demo.wav"
