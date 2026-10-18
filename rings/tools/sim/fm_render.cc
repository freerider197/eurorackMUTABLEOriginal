#include "rings/dsp/fm_voice.h"
#include <cstdio>
#include <cstdlib>
using namespace rings;
// render <f0> <ratio> <bright> <pos> <damp> -> 1s of drone/pluck as float32 on stdout
int main(int c,char**v){ FMVoice x; x.Init(); float in[24]={0},o[24],a[24];
  x.set_frequency(atof(v[1])/kSampleRate); x.set_ratio(atof(v[2])); x.set_brightness(atof(v[3]));
  x.set_feedback_amount(atof(v[4])); x.set_damping(atof(v[5]));
  for(int i=0;i<200;i++) x.Process(in,o,a,24); x.TriggerInternalEnvelope();
  for(int b=0;b<2000;b++){x.Process(in,o,a,24); fwrite(o,4,24,stdout);} }
