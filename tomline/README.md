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
firmware's LEDs and setup warnings. With a mouse only: click METER to latch the
second page, and click **Hold FN 2 · Tune** to keep the FN 2 page open (click
it or FN 2 again to let go).

When `voice.cc` or `parameters.cc` changes, update the block between
`BEGIN FIRMWARE PORT` and `END FIRMWARE PORT` in `browser/tomline.html`, run
the comparison (see Tests), then `python3 tomline/tools/make_standalone.py`.

## Patch and setup

| Jack / control | Setting |
|---|---|
| EXCITE 1 | Trigger (on above about 1.7 V, off below 0.85 V, stock thresholds) |
| EXCITE 2 | V/oct. 0 V = C1 (32.7 Hz) with Tune (FN 2 + SHAPE 1) fully down, the default; C2 at noon |
| OUT 1 | The sound, swinging around +3.33 V (0 V to about +6.7 V, see "Offset"). Use an AC-coupled input (Erica Black Quad VCA2: IN 1–3, not IN 4) |
| IN 1 | Not used, leave empty |
| Channel 1 response trimmer (small magenta knob) | **Fully clockwise (LIN)** |
| LEVEL MOD 1 (big magenta knob) | **Fully counter-clockwise** |
| LEVEL 1, IN 2, LEVEL 2, OUT 2, LEVEL MOD 2, trimmer 2 | Not used |

If LEVEL MOD 1 or the channel 1 trimmer is set wrong, LED 3 of the left column
blinks red.

## Controls

| Control | Job | Hold METER (or tap METER to latch) |
|---|---|---|
| SHAPE 1 | Decay, 95 ms to 2.5 s. **FN 2 held: Tune**, ±12 semitones; fully CCW = C1 at 0 V (the default), noon = C2. Saved, so a bump on the main page can't detune it | Crush. Fully CCW = off; just past it the sample rate drops to 6 kHz, and turning up lowers it to 750 Hz at fully CW: ring tones and grit on top of the note. Best heard with Tone open |
| MOD 1 | Tone: filter envelope. Darkest at 7 o'clock (the filter stops just above the note, so it gets dark but never silent), most envelope at noon, open at 5 o'clock (stock Streams curve). **FN 2 held: Glide** between notes (fully CCW = off) | Pitch LFO amount (depth): fully CCW = off, up to ±12 semitones |
| SHAPE 2 | Wave of the main tone. Fully CCW = sine (the usual sound), then triangle (9 o'clock to 11), folded sine (the top of the wave folds back: brighter and grittier, the bass stays), and a soft-edged square at fully CW. The level stays even along the knob (within 1 dB). The snap doesn't change | Pitch sweep. Fully CCW = off (the default); turning up makes each hit start higher, up to +12 semitones at fully CW (+3 at noon), and fall back to the note with a 20 ms time constant, so the held note stays in tune |
| MOD 2 | Soft distortion. Fully CCW = clean; turning up drives the whole sound (tone and snap) into a soft clipper, up to +12 dB (+5 dB at noon). The loud start of each hit saturates, the quiet tail stays clean but gets louder, so the decay sounds longer. A gain after the clipper keeps each hit as loud as the clean sound (within 1 dB at any Decay and Wave), so the knob changes colour, not volume | Pitch LFO speed, 0.05 to 20 Hz (1 Hz at noon) |
| FN 1 (left button) | Engine: BT / LT | LFO restarts on each trigger (default) / free running |
| FN 2 (right button), tap | Snap, the attack click (a burst of noise and a quick pitch blip): off, 0.4, 1.5, 4 ms. Off has no click at all; the tone still fades in over 0.4 ms, so a hit never pops | LFO wave: triangle / ramp up. FN 2 doesn't open its own page from here |
| FN 2, hold | Pitch layer: SHAPE 1 = Tune, MOD 1 = Glide. Nothing else (SHAPE 2, MOD 2, FN 1 and METER do nothing here). Everything LFO lives on the METER page | |
| METER, tap | Latch / unlatch the second layer | |
| METER, hold 3 s without turning a knob | Stock offset calibration (see Calibration) | |
| FN 1 + FN 2, hold 1 s | V/oct calibration | |

Pick-up, on every page: when a page opens, each value stays where it was
until you turn its knob. Then the value goes to the knob position (a quick
40 ms glide, so it never clicks) and follows the knob. A knob that already
sits on its value follows it straight away. The knobs are read while all
LEDs are off (each page lights other LEDs, which shifted every knob reading
and made all values jump on a page change), so the LEDs are a quarter dimmer
than before, and readings settle for 50 ms after a page change before a turn
counts. A button tap only does its job if
no knob was turned and no other button was pressed while it was down (the
FN 1 and FN 2 taps on the METER page are its LFO buttons): turning a knob
while holding METER or FN 2 never latches, calibrates or changes snap. The LFO
always goes to pitch and is added after the glide. By default every trigger
restarts it at the middle of its wave (no pitch jump, going up), so each hit
gets the same pitch movement; free running, it ignores triggers.

Engine, snap, crush, wave, glide, LFO speed, amount, wave and free run, Tune, sweep, and the
V/oct calibration are saved to flash 2 s after the last change. Settings saved
by an older Tomline (before the LFO) are read once: engine, snap, second-layer
values and V/oct calibration carry over, and the LFO starts off. Settings saved
before the free-run option read as "restart on trigger". Settings saved before
the wave start with the wave at sine (that slot held an unused old value);
everything else carries over. Settings saved before Crush start with Crush
off (that slot was the noise level; the noise is gone). Settings saved
before Tune moved to FN 2 start with Tune fully down (C1 at 0 V). Settings saved before the sweep start with the sweep off. A snap saved before
"off" existed keeps its length (10 ms becomes 4 ms). Going back to an older
Tomline keeps all settings except free run, which reads as "restart on
trigger", and plays the snap one step longer.

## LEDs

| LED | Meaning |
|---|---|
| Left column, top | BT engine |
| Left column, 2nd | LT engine |
| Left column, 3rd | Blinking red: setup warning (LEVEL MOD 1 not at zero, checked in the first second after power-up, or trimmer 1 not at LIN) |
| Left column, bottom | Flashes green on each trigger |
| Right column (normal page) | Snap, from the bottom: no LED = off, 1 = 0.4 ms, 2 = 1.5 ms, 3 = 4 ms |
| Engine and snap LEDs amber instead of green | Second layer active |
| METER page: right column top (triangle) or 2nd (ramp up) pulsing amber | LFO wave; the pulse follows the LFO, so its speed shows even at zero amount |
| METER page: right column 3rd LED amber | LFO free running (off = restarts on each trigger) |
| FN 2 held: engine LED red, right column off | Tune and glide page |

## Offset

The VCA can only scale a fixed positive source (+3.33 V when IN 1 is empty),
so OUT 1 can't go below 0 V while a note sounds. Firmware can't remove that.
Tomline writes the wave on top of a fixed lift: OUT 1 sits at +3.33 V and
the wave swings around it, so nothing jumps when a note starts. (Up to 0.4,
the lift followed each note's peak and OUT 1's average level jumped by about
2.4 V on every hit: a click that Tone and snap could not remove.)
AC-coupled inputs remove the steady 3.33 V. A DC-coupled input passes it on,
and a VCA after it turns every change of its gain (a sidechain duck) into a
thump.

At power-up OUT 1 stays at 0 V for 1 s (the LEVEL MOD 1 check needs the VCA
at zero), then rises slowly to 3.33 V over about 1 s.

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
unplugged, hold METER for 3 s. Then patch an oscillator into IN 1, monitor OUT
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

Heard on the module by the owner: the soft-distortion build (test file
`tomline-test-distortion-5d530a7.wav`, merged in #24) flashed and sounded good.
Not measured yet:

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

Last run: worst case 596 bytes (main loop 332, SysTick 100, audio interrupt
164), 4096 reserved, 3500 to spare. The
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
LFO at zero depth leaves the sound unchanged, and that a very loud mix clips
instead of wrapping around, and that Tone fully
closed keeps each note within 6 dB of Tone fully open (BT and LT, 0 to 4 V),
and the Wave stages (sine, triangle, folded, square): the fundamental stays
within 6 dB, the triangle has its 5th harmonic, the fold adds bright
harmonics, the square has odd harmonics, and the level stays within 1 dB,
that MOD 2 keeps the hit's level within 1 dB at every Decay and Wave,
and Crush: the sample rate just past off, at noon and at full, and that the
level stays, and that snap off has no click noise or pitch blip.
It also checks that OUT 1 rests at 3.33 V, that its average level doesn't jump
on a hit, and the power-up (0 V for 1 s, then a slow rise). It exits
with an error if a check fails.

Last run: OUT 1 between 0.30 V and 6.49 V, back to 3.330 V after the
notes. Attack jump (average over 100 ms after a hit minus before): BT -0.04 V,
LT +0.05 V, BT with Tone closed and snap off +0.11 V (the moving lift of 0.4:
+2.3 to +2.4 V). Power-up: OK. Pitch at 0, 1, 2 and 3 V within 0.1 cent of C2, C3, C4, C5. Tune
range C1 / C2 / C3: OK. Glide C2 to C3: -0.07 cents. LFO knobs: OK. LFO at full
depth, 0.2-1.2 s, restart on trigger: triangle +1.71 st, ramp up +0.86 st; free
running: triangle -10.30 st, ramp up -11.14 st (all within 0.03 st of the
expected value). LFO restart on every trigger / free run: OK. LFO at zero depth:
unchanged. Distortion (MOD 2 off / noon / full), 3rd harmonic at the start
of a hit: BT -16.8 / -14.1 / -10.6 dB, LT -21.6 / -17.1 / -11.4 dB; the quiet
tail +0.0 / +1.8 / +6.0 dB against the hit, as designed. Hit level (first
0.3 s) along MOD 2, every Decay and Wave: BT -0.9 to +0.3 dB, LT -0.7 to +1.0 dB
(before the level correction: up to +6.6 dB). Wave (sine / triangle / folded /
square), fundamental: BT +0.0 / -0.2 / -3.3 / -0.6 dB; level along the knob:
BT -0.5 to +0.1 dB, LT -0.1 to +0.2 dB (without the level matching: about
-1.7 to +1.6 dB); triangle 5th harmonic 1 s into a long note BT -36.1 dB (sine
-40.8), LT -42.6 dB (sine -58.3): the level matching
clips the triangle's tips a little; square 3rd harmonic BT -8.3, LT -9.6 dB (ideal -9.5).
Snap off: largest jump in the first 3 ms of a hit BT 0.08 V, LT 0.08 V (0.4 ms
snap: 0.27 / 0.19 V).
Crush sample rate (just past off / noon / full): 5856 / 2120 / 748 Hz
(expected 5868 / 2121 / 750), level within 0.1 dB. Tone closed vs open: BT -4.5 to
-4.7 dB, LT -4.3 to -4.6 dB at every
voltage (-2.6 dB with the moving lift, whose bump passed the closed filter;
-25 dB before the floor). Retrigger: worst one-sample jump BT 0.38 V, LT
0.25 V, 1.1 / 1.1 x a note from silence (0.35 / 0.23 V); with the moving lift
0.45 / 0.36 V against 0.51 / 0.42 V. Loud mix: clips
correctly. Knob jump, end to end in the middle of a note (8 values, BT and
LT): with the 40 ms glide no one-sample jump is bigger than the note's own
at the settings the glide passes through (the folded wave, half way along
Wave, is spikier than both ends).

`tomline/test/panel.cc` runs the firmware's own `ui.cc` on a computer, with
stand-ins for the drivers in `tomline/test/stub/`, and plays knob and button
moves: pick-up on page changes, METER, FN 1 and FN 2 taps and holds with knobs
turned, two buttons at once, offset and V/oct calibration, flash saves, older
saved settings (the wave starts at sine, tune at C1), the knob map (SHAPE 2 wave; METER + MOD 1 / MOD 2 LFO amount / speed; FN 2 + SHAPE 1 / MOD 1 tune / glide), and
LEDs that shift the knob readings (a model of the effect seen on the module):

```
g++ -O2 -Itomline/test/stub -I. -o build/tomline_panel tomline/test/panel.cc tomline/voice.cc tomline/parameters.cc
./build/tomline_panel
```

Last run: all 68 checks OK. Without the LEDs-off reading, the model moves
untouched values on page changes; with it, nothing moves.

`tomline/test/words.cc` dumps the firmware's VCA and filter words for 13
patches (BT, LT, glide, everything at maximum, LFO triangle and ramp up, each
restarting and free running, the wave at triangle, folded and square, and
crush at two rates), and
`tomline/test/compare_browser.js` runs the browser port on the same input:

```
g++ -O2 -I. -o build/tomline_words tomline/test/words.cc tomline/voice.cc tomline/parameters.cc
./build/tomline_words
node tomline/test/compare_browser.js
```

Last run: identical in all 13 cases (244,627 samples each, zero
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
| `pickup.h` | Knob pick-up and the 40 ms glide to the knob (pure code, tested on a computer) |
| `test/render.cc` | Renders the voice with an analog model to WAV, checks levels, pitch, glide, knob jump clicks |
| `test/panel.cc`, `test/stub/` | Runs `ui.cc` on a computer: pick-up, buttons, calibration entry, saves |
| `test/words.cc`, `test/compare_browser.js` | Check the browser model against the firmware |
