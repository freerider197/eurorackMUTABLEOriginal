# A-weighted loudness (first 200ms) of two fm_render builds: python3 a_weighted_compare.py <old_render> <new_render>
import subprocess, sys, numpy as np
def aweight(f):
  f2 = f * f
  return (12194**2 * f2**2) / ((f2 + 20.6**2) * np.sqrt((f2 + 107.7**2) * (f2 + 737.9**2)) * (f2 + 12194**2)) / 0.794
def lev(R, *a):
  x = np.frombuffer(subprocess.run([R] + [str(v) for v in a], capture_output=True).stdout, dtype=np.float32)[:9600]  # 200 ms
  X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / 48000)
  return 10 * np.log10((np.abs(X)**2 * aweight(np.maximum(f, 1))**2).sum()), np.abs(x).max() * 0.7
old, new = sys.argv[1], sys.argv[2]
ref = lev(old, 220, 0.5, 0.5, 0.5, 1.0)[0]
print("A-weighted dB (first 200ms) vs v1 drone @ bright .5, f0 220/110 avg; peak")
print("damp  bright:   0.0           0.25          0.5           0.8")
for d in (0.0, 0.3, 0.6, 1.0):
  row = []
  for b in (0.0, 0.25, 0.5, 0.8):
    o = np.mean([lev(old, f, .5, b, .5, d)[0] for f in (110, 220)]) - ref
    n = [lev(new, f, .5, b, .5, d) for f in (110, 220)]
    row.append(f"{o:+5.1f}->{np.mean([x[0] for x in n]) - ref:+5.1f} ")
  pk = max(lev(new, f, .5, b, .5, d)[1] for f in (110, 220) for b in (0, .25, .5, .8))
  print(f"{d:<4}         " + " ".join(row) + f"  pk {pk:.2f}")
