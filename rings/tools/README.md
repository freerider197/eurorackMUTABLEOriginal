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
BRIGHTNESS and POSITION (tables in `rings/dsp/fm_voice.cc`), -3dB overall.

If CPU gets tight, the Mini-Elements exciter (`Part::FillExciterBuffer` in
`rings/dsp/part.cc`) is the agreed candidate to remove.
