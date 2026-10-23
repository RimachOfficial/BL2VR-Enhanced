// filter_bench.cpp - offline test of the REAL filter code (openvr_proxy/src/filters.h).
// Build (from repo root): g++ -O2 -std=c++17 -I openvr_proxy tools/filter_bench.cpp -o filter_bench
//   (MSVC: cl /O2 /EHsc /std:c++17 /I openvr_proxy tools\filter_bench.cpp)
// Reports lag while moving, settle time after a stop, a 2-degree correction, and how much of an
// 8 Hz hand tremor is left, for the OLD profiles from the INI and the NEW ones with catch-up.
#include "src/filters.h"
#include <cstdio>
#include <vector>
#include <cstdlib>
static const double D2R = kPi/180.0;
struct M { double lagSlow, lagFast, tail05, t90, keep; };
const double HZ=120, DT=1.0/HZ;

// signal in "units" (deg for rot, cm for pos); filter works in rad / m
static double filt(bool rot,double mc,double beta,double c,const std::vector<double>&raw,std::vector<double>&out){
  out.clear();
  if(rot){ OneEuroQuat f; f.catchup=c; for(double a:raw){ float q[4]={0,(float)sin(a*D2R/2),0,(float)cos(a*D2R/2)},o[4]; f.step(q,DT,mc,beta,1,o); out.push_back(2*atan2((double)o[1],(double)o[3])/D2R);} }
  else { OneEuroVec3 f; f.catchup=c; for(double a:raw){ float x[3]={(float)(a/100.0),0,0},o[3]; f.step(x,DT,mc,beta,1,0,nullptr,o); out.push_back(o[0]*100.0);} }
  return 0;
}
// rot: slow=15 deg/s fast=60 deg/s, tremor 0.3 deg, thresholds 0.5 deg, step 2 deg
// pos: slow=10 cm/s fast=60 cm/s, tremor 0.3 cm, thresholds 0.5 cm, step 2 cm
static M measure(bool rot,double mc,double beta,double c){
  double slow=rot?15:10, fast=rot?60:60, trem=0.3, thr=0.5, stepv=2.0; M m; std::vector<double> raw,out;
  for(int k=0;k<2;k++){ double sp=k?fast:slow; raw.clear(); for(int i=0;i<(int)(3*HZ);i++) raw.push_back(sp*i*DT); filt(rot,mc,beta,c,raw,out); double e=raw.back()-out.back(); if(k) m.lagFast=e; else m.lagSlow=e; }
  raw.clear(); int nm=(int)HZ; for(int i=0;i<nm;i++) raw.push_back(fast*i*DT); double sv=fast*(nm-1)*DT; for(int i=0;i<(int)(8*HZ);i++) raw.push_back(sv);
  filt(rot,mc,beta,c,raw,out); m.tail05=9; for(size_t i=nm;i<raw.size();i++) if(fabs(raw[i]-out[i])<thr){ m.tail05=(i-nm)*DT; break; }
  raw.clear(); for(int i=0;i<(int)HZ;i++) raw.push_back(0); for(int i=0;i<(int)(6*HZ);i++) raw.push_back(stepv);
  filt(rot,mc,beta,c,raw,out); m.t90=9; for(size_t i=(size_t)HZ;i<raw.size();i++) if(out[i]>=0.9*stepv){ m.t90=(i-HZ)*DT; break; }
  raw.clear(); for(int i=0;i<(int)(4*HZ);i++) raw.push_back(trem*sin(2*kPi*8.0*i*DT));
  filt(rot,mc,beta,c,raw,out); double sr=0,so=0; for(size_t i=(size_t)(2*HZ);i<raw.size();i++){sr+=raw[i]*raw[i];so+=out[i]*out[i];} m.keep=100*sqrt(so/sr);
  return m;
}
int grid_main_unused(int argc,char**argv){
  bool rot = argc>1 && atoi(argv[1])==1;
  double units = rot?1:1; (void)units;
  printf("%s grid. columns: mc(Hz) beta catchup | lag slow, lag fast | tail<0.5 | 2-step t90 | tremor kept%%\n", rot?"ROTATION (deg, per radian catchup)":"POSITION (cm, per metre catchup)");
  double mcs[]={0.3,0.5,0.8,1.5}; double betas[]={0.5,1.5,4}; 
  double cs_rot[]={0,30,60,120,240}; double cs_pos[]={0,30,60,120,240};
  for(double mc:mcs) for(double b:betas) for(double c:(rot?cs_rot:cs_pos)){
    M m=measure(rot,mc,b,c);
    printf("%4.2f %4.1f %5.0f | %5.2f %5.2f | %4.2f | %4.2f | %5.1f\n",mc,b,c,m.lagSlow,m.lagFast,m.tail05,m.t90,m.keep);
  }
}

struct Row{const char*n;bool rot;double mc,b,c;};
int main(){
 Row R[]={
  {"BASE  rot  now (c=0)        ",1,1.5,1.2,0},{"BASE  rot  +catchup 120     ",1,1.5,1.2,120},
  {"TRIG  rot  now (1.5/4,1.2/4)",1,0.375,0.3,0},{"TRIG  rot  NEW 0.30Hz b0.24 ",1,0.30,0.24,120},{"TRIG  rot  NEW 0.30Hz b1.2  ",1,0.30,1.2,120},
  {"2HND  rot  now (0.4,0.6)    ",1,0.4,0.6,0},{"2HND  rot  NEW 0.30Hz b1.5  ",1,0.30,1.5,120},
  {"BASE  pos  now (c=0)        ",0,4.0,2.0,0},{"BASE  pos  +catchup 150     ",0,4.0,2.0,150},
  {"TRIG  pos  now (.667,.333)  ",0,0.667,0.333,0},{"TRIG  pos  NEW 0.5Hz b.25  ",0,0.5,0.25,150},{"TRIG  pos  NEW 0.5Hz b1.5  ",0,0.5,1.5,150},
  {"2HND  pos  now (0.8,1.0)    ",0,0.8,1.0,0},{"2HND  pos  NEW 0.5Hz b1.5  ",0,0.5,1.5,150},
 };
 printf("%-30s| lag slow  lag fast | tail<.5 | 2-step t90 | tremor kept\n","profile (units: deg / cm)");
 for(auto&r:R){ M m=measure(r.rot,r.mc,r.b,r.c); printf("%-30s| %6.2f   %6.2f   | %5.2f s | %5.2f s    | %5.1f%%\n",r.n,m.lagSlow,m.lagFast,m.tail05,m.t90,m.keep);} return 0; }
