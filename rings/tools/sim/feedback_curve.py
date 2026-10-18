# How fast the sound changes vs POSITION distance from noon: python3 feedback_curve.py <fm_render build>
import subprocess, sys, numpy as np
R = sys.argv[1]
def spec(f0, r, b, p):
  x = np.frombuffer(subprocess.run([R, str(f0), str(r), str(b), str(p), '1'], capture_output=True).stdout, dtype=np.float32)[4800:48000]
  X = np.abs(np.fft.rfft(x * np.hanning(len(x))))**2
  # 1/3-octave bands, dB
  f = np.fft.rfftfreq(len(x), 1 / 48000); edges = 50 * 2 ** (np.arange(0, 28) / 3)
  return np.array([10 * np.log10(X[(f >= a) & (f < b2)].sum() + 1e-12) for a, b2 in zip(edges[:-1], edges[1:])])
def dist(f0, r, b, p):
  a, c = spec(f0, r, b, 0.5), spec(f0, r, b, p)
  fl = max(a.max(), c.max()) - 50
  return np.sqrt(np.mean((np.clip(a, fl, None) - np.clip(c, fl, None)) ** 2))
offs = [0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5]
for side, sgn in (("left (DX7)", -1), ("right (classic)", 1)):
  d = [np.mean([dist(f0, r, b, 0.5 + sgn * o) for f0 in (110, 220) for r in (0.3, 0.5) for b in (0.4, 0.7)]) for o in offs]
  print(f"{side:<16} knob distance from noon (fraction of half-turn): " + " ".join(f"{o*2:.1f}" for o in offs))
  print(f"{'':<16} change vs noon, % of max:                      " + " ".join(f"{100*v/d[-1]:4.0f}" for v in d))
