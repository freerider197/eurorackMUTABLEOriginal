#include "rings/dsp/fm_voice.h"
#include <cstdio>
#include <cmath>
using namespace rings;
static void run(float d,float br,float f0,float ratio,float pos,float*rms,float*pk){
  FMVoice v; v.Init(); float in[24]={0},out[24],aux[24];
  v.set_frequency(f0/kSampleRate); v.set_ratio(ratio); v.set_brightness(br);
  v.set_feedback_amount(pos); v.set_damping(d);
  for(int i=0;i<10;i++) v.Process(in,out,aux,24);
  v.TriggerInternalEnvelope(); double s=0; float p=0;
  for(int b=0;b<400;b++){v.Process(in,out,aux,24);for(int i=0;i<24;i++){float x=out[i]*0.7f;s+=x*x;p=fmaxf(p,fabsf(x));}}
  *rms=sqrt(s/9600);*pk=p;}
int main(int argc,char**argv){
  float ref=atof(argv[1]); // absolute RMS of the original drone (bright .5)
  printf("damp  dB(br.2) dB(br.5) dB(br.8)  peak\n");
  float br[3]={0.2f,0.5f,0.8f};
  for(int j=0;j<=10;j++){float d=j*0.1f,pm=0;printf("%.1f ",d);
    for(int a=0;a<3;a++){float r,p;run(d,br[a],220,0.5f,0.5f,&r,&p);pm=fmaxf(pm,p);printf("  %6.1f",20*log10(r/ref));}
    printf("   %.2f\n",pm);}
  printf("\nfeedback (POSITION) sweep, damping .3 / drone, dB vs original drone\n");
  for(int j=0;j<=10;j++){float r1,r2,p;run(0.3f,0.5f,220,0.5f,j*0.1f,&r1,&p);run(1.0f,0.5f,220,0.5f,j*0.1f,&r2,&p);
    printf("%.1f  %6.1f  %6.1f\n",j*0.1f,20*log10(r1/ref),20*log10(r2/ref));}
}
