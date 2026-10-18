#include "rings/dsp/fm_voice.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
using namespace rings;
// Plucks every 0.75s while DAMPING steps 0 -> 1 (then a 2s drone), same as the
// Rings output stage (model gain 0.7, limiter ignored). 16-bit mono wav.
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"wb"); int n=0; static int16_t buf[48000*20];
  FMVoice v; v.Init(); float in[24]={0},out[24],aux[24];
  v.set_frequency(147.f/kSampleRate); v.set_ratio(0.5f); v.set_brightness(0.55f); v.set_feedback_amount(0.5f);
  for(int k=0;k<=10;k++){ v.set_damping(k<10? k*0.09f : 1.0f); v.TriggerInternalEnvelope();
    int blocks=(k<10?36000:96000)/24;
    for(int b=0;b<blocks;b++){v.Process(in,out,aux,24);for(int i=0;i<24;i++){float x=out[i]*0.7f*0.8f; x=x>1?1:x<-1?-1:x; buf[n++]=(int16_t)(x*32767);}}}
  uint32_t bytes=n*2,sr=48000,br=96000,fmt=16,riff=36+bytes; uint16_t pcm=1,ch=1,ba=2,bits=16;
  fwrite("RIFF",1,4,f);fwrite(&riff,4,1,f);fwrite("WAVEfmt ",1,8,f);fwrite(&fmt,4,1,f);fwrite(&pcm,2,1,f);fwrite(&ch,2,1,f);
  fwrite(&sr,4,1,f);fwrite(&br,4,1,f);fwrite(&ba,2,1,f);fwrite(&bits,2,1,f);fwrite("data",1,4,f);fwrite(&bytes,4,1,f);fwrite(buf,2,n,f);fclose(f);}
