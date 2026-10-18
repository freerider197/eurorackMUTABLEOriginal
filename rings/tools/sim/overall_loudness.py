# Loudness spread over all knob combinations: python3 overall_loudness.py <fm_render build> [...]
import subprocess, sys, numpy as np
def aw(f):
  f2=f*f; return (12194**2*f2**2)/((f2+20.6**2)*np.sqrt((f2+107.7**2)*(f2+737.9**2))*(f2+12194**2))/0.794
def lev(R,f0,r,b,p,d):
  x=np.frombuffer(subprocess.run([R,str(f0),str(r),str(b),str(p),str(d)],capture_output=True).stdout,dtype=np.float32)
  y=x[:9600]; X=np.fft.rfft(y); f=np.fft.rfftfreq(len(y),1/48000)
  return 10*np.log10((np.abs(X)**2*aw(np.maximum(f,1))**2).sum()), np.abs(x).max()*0.7
F=(82,165,330); RR=(.1,.5,.9)
ref=None
for R in sys.argv[1:]:
  v=[];pk=0
  for d in (0,.25,.5,.75,1):
    for b in (0,.25,.5,.75,1):
      for p in (0,.25,.5,.75,1):
        m=[lev(R,f,r,b,p,d) for f in F for r in RR]; v.append(np.mean([x[0] for x in m])); pk=max(pk,max(x[1] for x in m))
  v=np.array(v)
  if ref is None: ref=v.max()
  print(f"{R.split('/')[-1]:<13} all knobs (damp x bright x pos, 125 cells): loudest {v.max()-ref:+.1f}  quietest {v.min()-ref:+.1f}  spread {v.max()-v.min():.1f} dB  std {v.std():.1f}  peak {pk:.2f}")
