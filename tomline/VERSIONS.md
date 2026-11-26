# Tomline builds

Every `.wav` handed to the owner gets the next number. The latest build is the highest number.

**Latest: v18** · **Next build: v19**

Name a new build `tomline-vNN-<topic>.wav`, with no commit code, date or "test" in the name (rules in `AGENTS.md`,
"Build numbers"). Add its row here and raise "Next build" by one, in the same PR.

| Build | File | Handed over | Commit | What changed | Old file name |
| --- | --- | --- | --- | --- | --- |
| v01 | `tomline-v01-tune-lfo.wav` | 19 Nov 09:07 | – | Tune LFO test | `tomline-tune-lfo-test.wav` |
| v02 | `tomline-v02-no-in1.wav` | 19 Nov 09:34 | – | No IN 1 | `tomline-no-in1-test.wav` |
| v03 | `tomline-v03-test3.wav` | 19 Nov 09:57 | – | Same file as `release/tomline-0.2.wav` | `tomline-0.2-test3.wav` |
| v04 | `tomline-v04.wav` | 19 Nov 10:24 | – | Same file as `release/tomline-0.3.wav` | `tomline-0.3.wav` |
| v05 | `tomline-v05.wav` | 19 Nov 11:11 | – | Same file as `release/tomline-0.4.wav` | `tomline-0.4.wav` |
| v06 | `tomline-v06.wav` | 19 Nov 12:04 | – | | `tomline.wav` |
| v07 | `tomline-v07-pickup.wav` | 19 Nov 13:10 | `25c2ed2` | Knob pick-up | `tomline-test-pickup-25c2ed2.wav` |
| v08 | `tomline-v08-ledsoff.wav` | 19 Nov 13:27 | `edcbd18` | LEDs off | `tomline-test-ledsoff-edcbd18.wav` |
| v09 | `tomline-v09-fixed-lift.wav` | 19 Nov 14:15 | `d88602e` | Fixed lift | `tomline-test-fixed-lift-d88602e.wav` |
| v10 | `tomline-v10-distortion.wav` | 19 Nov 14:38 | `5d530a7` | Soft distortion (heard on the module: good) | `tomline-test-distortion-5d530a7.wav` |
| v11 | `tomline-v11-merged.wav` | 19 Nov 21:43 | `879934b` | The tests above merged | `tomline-test-merged-879934b.wav` |
| v12 | `tomline-v12-sweep.wav` | 26 Nov 08:01 | `c061b0c` | Test pitch sweep | `tomline-test-sweep-c061b0c.wav` |
| v13 | `tomline-v13-sweep-saved.wav` | 26 Nov 08:10 | `14c2177` | Pitch sweep saved with the other settings | `tomline-test-sweep-saved-14c2177.wav` |
| v14 | `tomline-v14-lfo-amber.wav` | 26 Nov 08:26 | `3f473f4` | LFO wave and free-run buttons and LEDs on the METER page | `tomline-test-lfo-amber-3f473f4.wav` |
| v15 | `tomline-v15-kick-duck.wav` | 26 Nov | `f9d5dca` | Kick sidechain duck: kick into IN 2, FN 1 held + SHAPE 2 depth, + MOD 2 release (#30) | – |
| v16 | `tomline-v16-duck-release.wav` | 26 Nov | `e764bfc` | Duck release counted from each kick hit, smooth return (#30) (tried on the module: works, owner likes it) | – |
| v17 | `tomline-v17-level2-test.wav` | 26 Nov | `4f3d6cd` | LEVEL 2 read test: FN 2 held, the right column shows the CV on LEVEL 2. Sound unchanged (#33) (tried on the module: the meter follows LEVEL MOD 2 and the CV; +5 V fills it at LEVEL MOD 2 about 2 o'clock, red past that) | – |

`release/tomline-0.1.wav` came before v01. "Handed over" is when the file reached the owner's Mac.
