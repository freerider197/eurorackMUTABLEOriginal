# Tomline

Alternative firmware for the Mutable Instruments **Streams** hardware: a bass
tom / low tom voice for basslines, in the spirit of the bass tom (BT) and low
tom (LT) of an analog drum machine. Built on the Streams drivers and
calibration code from this repository (MIT license, Emilie Gillet).

**Status: untested on a real module.** It compiles, the audio update file
decodes correctly, and the DSP was run on a computer (see Tests). Everything
marked *unverified* below needs a real Streams to confirm.

## Patch and setup

| Jack / control | Setting |
|---|---|
| EXCITE 1 | Trigger (on above about 1.7 V, off below 0.85 V, stock thresholds) |
| EXCITE 2 | V/oct. 0 V = C1 (32.7 Hz) |
| OUT 1 | The sound, 0 V to about +6.7 V (see "Offset") |
| IN 1 | Leave empty. If you patch a VCO here, Tomline shapes the VCO instead |
| Channel 1 response trimmer (small magenta knob) | **Fully clockwise (LIN)** |
| LEVEL MOD 1 (big magenta knob) | **Fully counter-clockwise** |
| LEVEL 1, IN 2, LEVEL 2, OUT 2, LEVEL MOD 2, trimmer 2 | Not used |

If LEVEL MOD 1 or the channel 1 trimmer is set wrong, LED 3 of the left column
blinks red.

## Controls

| Control | Job | Hold METER (or tap METER to latch) |
|---|---|---|
| SHAPE 1 | Decay, 15 ms to 2.5 s | Noise level |
| MOD 1 | Tone: filter envelope. Closed at 7 o'clock, most envelope at noon, open at 5 o'clock (stock Streams curve) | +1 octave level |
| SHAPE 2 | Tune, ±24 semitones | Sweep time |
| MOD 2 | Sweep depth: pitch drop on each hit (up to 12 st on BT, 24 st on LT) | Glide between notes (fully CCW = off) |
| FN 1 (left button) | Engine: BT / LT | |
| FN 2 (right button) | Snap 0–3: length of the attack click (0.4, 1.5, 4, 10 ms) | |
| METER, tap | Latch / unlatch the second layer | |
| METER, hold 1 s without turning a knob | Stock offset calibration (see Calibration) | |
| FN 1 + FN 2, hold 1 s | V/oct calibration | |

Second-layer knobs use pick-up: a value only moves once the knob passes it,
so switching layers never makes a parameter jump. Engine, snap, the four
second-layer values and the V/oct calibration are saved to flash 2 s after
the last change.

## LEDs

| LED | Meaning |
|---|---|
| Left column, top | BT engine |
| Left column, 2nd | LT engine |
| Left column, 3rd | Blinking red: setup warning (LEVEL MOD 1 not at zero, or trimmer 1 not at LIN). Steady orange: IN 1 is patched, external mode |
| Left column, bottom | Flashes green on each trigger |
| Right column | Snap amount, from the bottom (1 LED = snap 0, 4 LEDs = snap 3) |
| Engine and snap LEDs amber instead of green | Second layer active |

## Offset

The VCA can only scale a fixed positive source (+3.33 V when IN 1 is empty),
so OUT 1 can't go below 0 V while a note sounds. Firmware can't remove that.
Tomline lifts the wave by its own peak level, so OUT 1 rests at 0 V between
notes and the lift falls with the decay. What remains is a low bump that
follows each hit: AC-coupled inputs remove it, DC-coupled ones pass it on.

## Flashing (audio bootloader, unchanged)

From the Streams manual: connect your audio interface output to channel 1's
**EXCITE** input. Power on the system with channel 1's function button (FN 1)
held: the four left LEDs blink orange. Set channel 1's **SHAPE** knob to its
middle position. Turn speakers off and silence other sounds on the computer,
then play `release/tomline-0.1.wav`. On red blinking, press FN 1 to retry and
adjust the level with SHAPE 1 (best with 2 green and 1 orange LED lit on the
right).

To go back to the stock firmware, flash `release/streams_1.01_original.wav`
(the official Mutable Instruments file) the same way.

Tomline reads the module's existing calibration and does not change the
bootloader.

## Calibration

**V/oct (do this once).** Hold FN 1 + FN 2 for 1 s. The top two left LEDs
light and the top two right LEDs blink: send exactly **1 V** into EXCITE 2,
press FN 2. The bottom LEDs now blink: send exactly **3 V**, press FN 2. All
LEDs flash green when saved, red if the reading was out of range (nothing is
saved). FN 1 cancels.

**Offsets (only if the module bleeds).** Same as the stock procedure: set both
response trimmers to LIN, patch cables into all 6 inputs with the other ends
unplugged, hold METER for 1 s. Then patch an oscillator into IN 1, monitor OUT
1 and turn SHAPE 1 until the bleed just disappears; same for channel 2 with
SHAPE 2. Press any button to save.

## Hardware map (from the source code and the v05 schematic)

Every row below was read from the code in `streams/` and the schematic
`streams_v05.pdf` (mutable-instruments-documentation), not guessed.

| Item | Wiring | Firmware access | Source |
|---|---|---|---|
| SHAPE 1 / MOD 1 pots | PA2 / PA3 (ADC2) | `adc.pot(0)` / `adc.pot(1)` | `drivers/adc.cc` pots_sequence_, `ui.cc` OnPotMoved |
| SHAPE 2 / MOD 2 pots | PA1 / PA0 (ADC2) | `adc.pot(2)` / `adc.pot(3)` | same |
| Pot scan | One pot per ms, 12-bit left-aligned | `Adc::ScanPots()` at 1 kHz | `ui.cc` Poll |
| FN 1 / FN 2 / METER | PB5 / PB4 / PB6, active low, pull-up | `switches` 0 / 1 / 2, debounced at 1 kHz | `drivers/switches.cc` |
| EXCITE 1 / EXCITE 2 | J1, x0.2 inverting buffer + 1.67 V offset, 8 kHz RC, PB0 / PA6 | `cv_scaler.excite_sample(0/1)`, about 3972 counts per volt, range about ±8.2 V | `drivers/adc.cc`, schematic IC4A/IC4B |
| IN 1 / IN 2 | J2, normalled to +10 V through 100k; with the VCA and ADC loads it sits at +3.33 V. Read on PA7 / PB1 | `cv_scaler.audio_sample(0/1)` | schematic R59, R42, R12 |
| VCA control sense | Op-amp output via 1k + BAT54S clamp, PA4 / PA5 | `cv_scaler.raw_gain_sample(0/1)`; above 59000 = VCA closed | `ui.cc` calibration |
| VCA gain (DAC) | DAC8552 on SPI2 (PB13/PB15/PB12). `dac.Write(0)` = output B = channel 1, `Write(1)` = output A = channel 2. Two RC stages after the DAC (1k/10 nF, 10k/1 nF, about 16 kHz each) | `dac.Write(ch, cv_scaler.ScaleGain(ch, word))` | `drivers/dac.h`, schematic sheet 1 |
| VCA law | 2164 cell in the control op-amp's feedback. Response trimmer at LIN: gain = word / 32767 (max 2x at 65535). Toward EXP: gain = (word/32767)^n, n up to about 20 | | `gain.h`, schematic IC5B/IC8C |
| LEVEL input | Current into the same VCA control node through LEVEL MOD. Normalled to +10 V through 24.9k. Not readable directly, only through the sense line | | schematic R34, R32, R60 |
| Filter | Two cascaded 1-pole low-pass stages built from 2164 cells, no resonance, DC gain 1 | | schematic IC8A/IC8B, IC7 |
| Filter cutoff | TIM1 CH3 (PA10) / CH4 (PA11), 10-bit PWM at 70 kHz, firmware smoothing 1/16 per sample, then RC 1.2k/680 nF (195 Hz) into the 2164 control pins | `pwm.Write(ch, 65535 - frequency)`: 65535 = open | `drivers/pwm.h`, schematic R4/C6, R5/C7 |
| OUT 1 / OUT 2 | TL072 through 1k, DC-coupled | | schematic J6 |
| LEDs | 8 bi-colour LEDs on an MCP23S17, bit-banged SPI (PB10 data, PB11 clock, PB7 select), 8 brightness levels | `leds.set(column*4 + row, red, green)`, row 0 = top, column 0 = left | `drivers/leds.h` |
| Sample clock | ADC1 scans the 6 inputs at 31,088 Hz; the DMA interrupt runs the audio code | `Process()` | `drivers/adc.cc` |
| UI clock | SysTick 4 kHz | `ui.Poll()` | `drivers/system.cc` |
| Flash | Bootloader 0x08000000 (16 KB), app 0x08004000 (112 KB), settings 0x0801F000 (4 pages), calibration 0x0801FC00 | | `stmlib/linker_scripts`, `cv_scaler.cc`, `ui.cc` |
| Bootloader entry | FN 1 held at power-on, audio into EXCITE 1 | | `bootloader/bootloader.cc` |

## Unverified (needs a real module)

- Peak output level (about 6.7 V expected, from gain.h and the schematic).
- Filter cutoff range in Hz. The model uses nominal 2164 values (48 kHz open, about 1 octave per 3950 PWM codes).
- The setup-warning thresholds (VCA sense readings in LIN versus EXP).
- V/oct tracking before calibration: resistor and 3.3 V tolerances can be off by a few percent, so calibrate.
- CPU time in the audio interrupt. Estimated well under the budget (the voice is about 400 instructions, the budget is 2316 cycles per sample), not measured.
- The BT and LT sounds are modeled by ear, not from Elektron's circuits.

## Building

From the root of this repository, with `arm-none-eabi-gcc` on the path
(tested with 13.2 from Ubuntu 24.04; the stock Streams code also builds with
it):

```
make -f tomline/makefile TOOLCHAIN_PATH=/usr/ wav
```

Output: `build/tomline/tomline.bin` and `build/tomline/tomline.wav`. The
makefile uses `tomline/tools/encoder.py`, a Python 3 port of the bootloader
encoder (needs numpy). `tomline/tools/check_wav.py` decodes a WAV and checks
every packet: the official `streams_1.01.wav` decodes 112/112, and the
Tomline WAV decodes back to exactly `tomline.bin`.

With the Vagrant environment (mutable-dev-environment, gcc 4.8, Python 2):
`make -f tomline/makefile bin` should work, then run
`python3 tomline/tools/encoder.py -s 48000 -b 12000 -c 6000 -p 256 build/tomline/tomline.bin`.
Not tried.

## Tests

`tomline/test/render.cc` runs the firmware's own `voice.cc` and
`parameters.cc` on a computer, with a model of the analog stage, and writes
`build/tomline_*.wav`:

```
g++ -O2 -I. -o build/tomline_render tomline/test/render.cc tomline/voice.cc tomline/parameters.cc
./build/tomline_render
```

It also checks that glide lands on the target note and that a very loud
mix (noise at 100 %, hard hit) clips instead of wrapping around. It exits
with an error if a check fails.

Last run: OUT 1 between 0.000 V and 6.66 V, back to about 0.001 V after the
notes. Pitch at 0, 1, 2 and 3 V within 0.1 cent of C1, C2, C3, C4. Glide
C2 to C3: -0.06 cents. Loud mix: clips correctly.

## Files

| File | What it does |
|---|---|
| `tomline.cc` | Init, main loop, audio interrupt (trigger detection, DAC and PWM writes) |
| `voice.cc/.h` | The voice, fixed point, runs at 31,088 Hz |
| `parameters.cc/.h` | Knob values to voice parameters (control rate, float) |
| `ui.cc/.h` | Knobs, buttons, LEDs, calibration, saved settings |
| `cv_scaler_import.cc` | Builds the stock `streams/cv_scaler.cc` unchanged |
| `tables.h` | Sine and 2^x tables, generated by `tools/make_tables.py` |
