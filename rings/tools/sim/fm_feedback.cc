#include "rings/dsp/fm_voice.h"
#include <cstdio>
#include <cmath>
using namespace rings;
// How much the drone's waveform changes vs. POSITION at noon (no feedback),
// as normalized RMS difference. 0 = identical (dead zone).
static void render(float pos,float*buf){
  FMVoice v; v.Init(); float in[24]={0},out[24],aux[24];
  v.set_frequency(220.f/kSampleRate); v.set_ratio(0.5f); v.set_brightness(0.5f);
  v.set_feedback_amount(pos); v.set_damping(1.0f);
  for(int i=0;i<200;i++) v.Process(in,out,aux,24);
  for(int b=0;b<400;b++){v.Process(in,out,aux,24);for(int i=0;i<24;i++)buf[b*24+i]=out[i];}}
int main(){ static float c[9600],x[9600]; render(0.5f,c);
  for(int j=0;j<=20;j++){float p=j*0.05f; render(p,x); double e=0,n=0;
    for(int i=0;i<9600;i++){e+=(x[i]-c[i])*(x[i]-c[i]);n+=c[i]*c[i];}
    printf("%.2f %.3f\n",p,sqrt(e/n));}}
