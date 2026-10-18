# A-weighted loudness grid (BRIGHTNESS x POSITION) at two DAMPING values: python3 loudness_grid.py <new fm_render> <reference fm_render>
import subprocess, sys, numpy as np
def aw(f):
  f2=f*f; return (12194**2*f2**2)/((f2+20.6**2)*np.sqrt((f2+107.7**2)*(f2+737.9**2))*(f2+12194**2))/0.794
def lev(R,f0,r,b,p,d):
  x=np.frombuffer(subprocess.run([R,str(f0),str(r),str(b),str(p),str(d)],capture_output=True).stdout,dtype=np.float32)
  y=x[:9600]; X=np.fft.rfft(y); f=np.fft.rfftfreq(len(y),1/48000)
  return 10*np.log10((np.abs(X)**2*aw(np.maximum(f,1))**2).sum()), np.abs(x).max()*0.7, abs(x[24000:].mean())
R=sys.argv[1]; V1=sys.argv[2]
F=(82,165,330); RR=(.1,.3,.5,.7,.9)
B=(0,.2,.35,.5,.75,1); P=(0,.2,.35,.5,.65,.8,1)
ref=np.mean([lev(V1,f,r,.5,.5,1)[0] for f in F for r in RR])
for name,Rx in (("v1",V1),("new",R)):
 for d in (0.25,1.0):
  allv=[]; pk=0; dc=0
  print(f"\n{name}  damping {d}: A-weighted dB vs v1 drone (avg ratios/pitches); cols POSITION")
  print("bright "+"".join(f"{p:>6}" for p in P))
  for b in B:
    row=[]
    for p in P:
      m=[lev(Rx,f,r,b,p,d) for f in F for r in RR]
      v=np.mean([x[0] for x in m])-ref; row.append(v); allv.append(v); pk=max(pk,max(x[1] for x in m)); dc=max(dc,max(x[2] for x in m))
    print(f"{b:<6} "+"".join(f"{v:+6.1f}" for v in row))
  print(f"spread {max(allv)-min(allv):.1f} dB, std {np.std(allv):.1f}, peak {pk:.2f}, max DC {dc:.3f}")
