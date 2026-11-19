# Tomline

Alternative firmware for the Mutable Instruments **Streams** hardware: a bass
tom / low tom voice for basslines, in the spirit of the bass tom (BT) and low
tom (LT) of an analog drum machine. Built on the Streams drivers and
calibration code from this repository (MIT license, Emilie Gillet).

**Status: untested on a real module.** It compiles, the audio update file
decodes correctly, and the DSP was run on a computer (see Tests). Everything
marked *unverified* below needs a real Streams to confirm.

## Browser model: try it here first

`browser/` holds a playable model of this firmware on the Streams panel
layout. Run tests and demos there before the module:

- Shared online version: https://claude.ai/artifact/Ad2fPxFnc93cGY7k4Hzxwt
  (private until you share it from the page's Share menu).
- Local copy: open `browser/index.html` in a browser.

The sound comes from a bit-exact JavaScript port of `voice.cc` and
`parameters.cc`: for the same knobs and notes it computes the same VCA and
filter words as the firmware, sample for sample (`test/compare_browser.js`
checks this). The analog part after the DAC is modeled from the schematic,
so levels and cutoff frequencies are estimates. The panel shows the
firmware's LEDs and setup warnings.

When `voice.cc` or `parameters.cc` changes, update the block between
`BEGIN FIRMWARE PORT` and `END FIRMWARE PORT` in `browser/tomline.html`, run
the comparison (see Tests), then `python3 tomline/tools/make_standalone.py`.

## Patch and setup

| Jack / control | Setting |
|---|---|
| EXCITE 1 | Trigger (on above about 1.7 V, off below 0.85 V, stock thresholds) |
| EXCITE 2 | V/oct. 0 V = C2 (65.4 Hz) with SHAPE 2 (Tune) at noon |
| OUT 1 | The sound, 0 V to about +6.7 V (see "Offset") |
| IN 1 | Not used, leave empty |
| Channel 1 response trimmer (small magenta knob) | **Fully clockwise (LIN)** |
| LEVEL MOD 1 (big magenta knob) | **Fully counter-clockwise** |
| LEVEL 1, IN 2, LEVEL 2, OUT 2, LEVEL MOD 2, trimmer 2 | Not used |

If LEVEL MOD 1 or the channel 1 trimmer is set wrong, LED 3 of the left column
blinks red.

## Controls

| Control | Job | Hold METER (or tap METER to latch) |
|---|---|---|
| SHAPE 1 | Decay, 15 ms to 2.5 s | Noise level |
| MOD 1 | Tone: filter envelope. Darkest at 7 o'clock (the filter stops just above the note, so it gets dark but never silent), most envelope at noon, open at 5 o'clock (stock Streams curve) | +1 octave level |
| SHAPE 2 | Tune, ±12 semitones. Noon = C2 at 0 V; the knob alone covers C1 to C3 | Sweep time: how fast the pitch falls back to the note (decay time constant about 4 to 180 ms on BT, 6 to 300 ms on LT) |
| MOD 2 | Sweep amount: 808/909-style pitch envelope. Each hit starts on the note, jumps up by this amount in 1 ms (up to 12 st on BT, 24 st on LT), then falls back to the note | Glide between notes (fully CCW = off) |
| FN 1 (left button) | Engine: BT / LT | |
| FN 2 (right button), tap | Snap 0–3: length of the attack click (0.4, 1.5, 4, 10 ms) | |
| FN 2, hold | Pitch LFO layer: SHAPE 1 = LFO speed (0.05 to 20 Hz, 1 Hz at noon), MOD 1 = LFO depth (fully CCW = off, up to ±12 semitones). SHAPE 2 and MOD 2 do nothing here | |
| FN 2 held + tap METER | LFO wave: triangle / ramp up | |
| FN 2 held + tap FN 1 | LFO restarts on each trigger (default) / free running | |
| METER, tap | Latch / unlatch the second layer | |
| METER, hold 1 s without turning a knob | Stock offset calibration (see Calibration) | |
| FN 1 + FN 2, hold 1 s | V/oct calibration | |

Second-layer and LFO knobs use pick-up: a value only moves once the knob
passes it, so switching layers never makes a parameter jump. Turning a knob
or tapping METER or FN 1 while FN 2 is held does not change snap. The LFO
always goes to pitch and is added after the glide. By default every trigger
restarts it at the middle of its wave (no pitch jump, going up), so each hit
gets the same pitch movement; free running, it ignores triggers.

Engine, snap, the four second-layer values, LFO speed, depth, wave and free run, and the
V/oct calibration are saved to flash 2 s after the last change. Settings saved
by an older Tomline (before the LFO) are read once: engine, snap, second-layer
values and V/oct calibration carry over, and the LFO starts off. Settings saved
before the free-run option read as "restart on trigger".

## LEDs

| LED | Meaning |
|---|---|
| Left column, top | BT engine |
| Left column, 2nd | LT engine |
| Left column, 3rd | Blinking red: setup warning (LEVEL MOD 1 not at zero, or trimmer 1 not at LIN) |
| Left column, bottom | Flashes green on each trigger |
| Right column | Snap amount, from the bottom (1 LED = snap 0, 4 LEDs = snap 3) |
| Engine and snap LEDs amber instead of green | Second layer active |
| FN 2 held: engine LED red, right column top (triangle) or 2nd (ramp up) pulsing red | LFO layer; the pulse follows the LFO, so the speed shows even at zero depth |
| FN 2 held: right column 3rd LED red | LFO free running (off = restarts on each trigger) |

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
then play `release/tomline-0.4.wav`. On red blinking, press FN 1 to retry and
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

Output: `build/tomline/tomline.bin` and `build/tomline/tomline.wav`.
The makefile links with `tomline/tomline.ld`: the same flash layout as
stmlib's Streams script, but a 4 KB stack instead of 512 bytes. Version 0.2
froze when knobs were turned: the knob maths (software floating point) plus
both interrupts needed 564 bytes, ran past the 512-byte stack and hit a fault.
Check the stack after every firmware change:

```
python3 tomline/tools/stack_usage.py build/tomline/tomline.elf
```

Last run: worst case 564 bytes (main loop 300, SysTick 100, audio interrupt
164), 4096 reserved, 3532 to spare. The
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

It also checks the tune range, that glide lands on the target note, the LFO
knob maths and wave shapes, the pitch of both LFO waves at full depth (restarting and free running),
that a trigger restarts the LFO only when not free running, that the
LFO at zero depth leaves the sound unchanged, and that a very loud mix (noise
at 100 %, hard hit) clips instead of wrapping around, and that Tone fully
closed keeps each note within 6 dB of Tone fully open (BT and LT, 0 to 4 V). It exits
with an error if a check fails.

Last run: OUT 1 between 0.000 V and 6.66 V, back to about 0.001 V after the
notes. Pitch at 0, 1, 2 and 3 V within 0.1 cent of C2, C3, C4, C5. Tune
range C1 / C2 / C3: OK. Glide C2 to C3: -0.07 cents. LFO knobs: OK. LFO at full
depth, 0.2-1.2 s, restart on trigger: triangle +1.71 st, ramp up +0.86 st; free
running: triangle -10.30 st, ramp up -11.14 st (all within 0.03 st of the
expected value). LFO restart on every trigger / free run: OK. LFO at zero depth:
unchanged. Pitch envelope, BT and LT: on
the note at the hit, peak +12 st (BT) / +24 st (LT) at 1.03 ms, back on the
note (0.000 cents) after 3 s, and a retrigger 10 ms in starts again from the
note. Tone closed vs open: BT -2.6 dB, LT -2.3 to
-2.4 dB at every voltage (was -25 dB before the floor). Loud mix: clips
correctly.

`tomline/test/words.cc` dumps the firmware's VCA and filter words for eight
patches (BT, LT, glide, everything at maximum, LFO triangle and ramp up, each
restarting and free running), and
`tomline/test/compare_browser.js` runs the browser port on the same input:

```
g++ -O2 -I. -o build/tomline_words tomline/test/words.cc tomline/voice.cc tomline/parameters.cc
./build/tomline_words
node tomline/test/compare_browser.js
```

Last run: identical in all eight cases (244,627 samples each, zero
difference).

## Files

| File | What it does |
|---|---|
| `tomline.cc` | Init, main loop, audio interrupt (trigger detection, DAC and PWM writes) |
| `voice.cc/.h` | The voice, fixed point, runs at 31,088 Hz |
| `parameters.cc/.h` | Knob values to voice parameters (control rate, float) |
| `ui.cc/.h` | Knobs, buttons, LEDs, calibration, saved settings |
| `cv_scaler_import.cc` | Builds the stock `streams/cv_scaler.cc` unchanged |
| `tables.h` | Sine and 2^x tables, generated by `tools/make_tables.py` |
| `browser/tomline.html` | Browser model (page body of the shared online version) |
| `browser/index.html` | Standalone browser model, made by `tools/make_standalone.py` |
| `test/render.cc` | Renders the voice with an analog model to WAV, checks levels, pitch, glide |
| `test/words.cc`, `test/compare_browser.js` | Check the browser model against the firmware |
