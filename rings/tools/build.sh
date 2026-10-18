#!/bin/bash
# Builds Rings with a modern arm-none-eabi-gcc (tested: 13.2.1, Ubuntu
# gcc-arm-none-eabi) and writes the audio update file. Upstream uses gcc 4.8.3;
# this script only overrides build flags, no source changes.
#
#   rings/tools/build.sh [output.wav]
#
# Needs: git submodule update --init stmlib stm_audio_bootloader, python3 + numpy.
set -e
cd "$(dirname "$0")/../.."
make -f rings/makefile -j8 \
  TOOLCHAIN_PATH="${TOOLCHAIN_PATH:-/usr/}" \
  LINKER_SCRIPT=rings/tools/stm32f4xx_flash_application_large_gcc13.ld \
  "CPPFLAGS=-fno-exceptions -fno-rtti -std=gnu++98 -Wno-error"
arm-none-eabi-size build/rings/rings.elf
python3 -I rings/tools/qpsk_encode.py build/rings/rings.bin \
  "${1:-build/rings/rings.wav}"
echo "Wrote ${1:-build/rings/rings.wav}"
