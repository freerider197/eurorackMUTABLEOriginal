# Rings tools (freerider197 fork)

Base: Lyle Mills' Mini-Elements Rings firmware (lylepmills/eurorack) plus FM
voice changes. Known-good on hardware: commit 20b257c (`alt_firmwares/rings_mini_elements_fm_loudness.wav`).

- `build.sh` builds the firmware with a modern gcc and writes the update .wav.
- `qpsk_encode.py` is a Python 3 port of `stm_audio_bootloader/qpsk/encoder.py`
  (stm32f4 settings). Checked against Lyle's shipped `rings_mini_elements.wav`:
  same length, samples within 1 LSB.
- `sim/run.sh` runs the FM voice offline (loudness / feedback / audio demo).
- `sim/fm_render.cc` renders one setting to raw float32 (stdout);
  `sim/overall_loudness.py` and `sim/loudness_grid.py` measure A-weighted
  loudness across knob combinations from one or more such builds.

FM voice (v2): POSITION left of noon = DX7-style modulator self-feedback,
right = original Rings feedback. Loudness compensated across DAMPING,
BRIGHTNESS and POSITION (tables in `rings/dsp/fm_voice.cc`), no overall trim.

If CPU gets tight, the Mini-Elements exciter (`Part::FillExciterBuffer` in
`rings/dsp/part.cc`) is the agreed candidate to remove.

v3 additions:
- POSITION has a response curve on each side of noon (kFeedbackCurve,
  kSelfFeedbackCurve in fm_voice.cc) for finer subtle amounts.
- Random per trigger: when the STRUCTURE, BRIGHTNESS, DAMPING or POSITION CV
  jack reads ~0V for 1s it's treated as empty; each trigger on the STRUM jack
  (not V/OCT) then picks a random value, scaled by that attenuverter
  (clockwise: above the knob, counter-clockwise: below). FM/frequency is
  never affected. See CvScaler::Read.

v4 (no options menu):
- Short RIGHT / LEFT: next / previous of 24 sounds = 6 resonators x 4 exciters
  (Rings, Elements bow, blow, strike), resonator-major. Long LEFT (2s):
  polyphony 1-2-3-4. Long RIGHT: easter egg on/off (short presses then pick
  its FX). Both short: frequency lock. Both long, right first: calibration.
- Right LED: color/motion = resonator (only the FM voice "breathes"), number
  of short blinks per 1.6s = exciter. Easter egg: fast flicker.
- Hold on Strum, alt chord tables, FM-input-to-timbre, Rings+Waveform and
  raw exciter out modes are gone (fixed to classic behaviour).
- bryanChords made const (moved 1.4KB from RAM to flash): the stack now has
  ~2.4KB instead of exactly 1KB.
- sim/run_v4_tests.sh: UI test with stubbed hardware, all-sounds stability
  test, and bootloader decode of an update .wav.
