// SPDX-License-Identifier: GPL-2.0
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
struct Enc { int32_t bitrate,rate; int16_t *pcm; uint8_t *bits; int16_t options[10]; };
struct Dec { int32_t rate; uint8_t *bits; int16_t *pcm; int16_t options[4]; int32_t bitrate; int16_t more[8]; };
static_assert(sizeof(void*)==4,"Stock codec is ARM32");
int main(int argc,char**argv) {
 void* lib=dlopen(argc>1?argv[1]:"/system/lib/libevs_float.so",RTLD_NOW|RTLD_LOCAL);
 if(!lib){printf("FAIL dlopen: %s\n",dlerror());return 1;}
 auto ec=(void*(*)(Enc*))dlsym(lib,"sEVSCreateEnc");
 auto ef=(int(*)(void*,Enc*))dlsym(lib,"sEVSEncFrame");
 auto ed=(void(*)(void*))dlsym(lib,"sEVSDeleteEnc");
 auto dc=(void*(*)(Dec*))dlsym(lib,"sEVSCreateDec");
 auto df=(int(*)(void*,Dec*))dlsym(lib,"sEVSDecFrame");
 auto dd=(void(*)(void*))dlsym(lib,"sEVSDeleteDec");
 if(!ec||!ef||!ed||!dc||!df||!dd)return 2;
 const int rate=argc>2?atoi(argv[2]):13200, bw=argc>3?atoi(argv[3]):2;
 const int dtx=argc>4?atoi(argv[4]):0;
 const int primary[]={2800,7200,8000,9600,13200,16400,24400,32000,48000,64000,96000,128000,2400,0,0,0};
 const int io[]={6600,8850,12650,14250,15850,18250,19850,23050,23850,1750,0,0,0,0,0,0};
 int16_t in[960]={},out[960]={}; uint8_t bits[512]={};
 Enc e={rate,48000,in,bits,{}};e.options[0]=dtx;e.options[5]=bw;
 Dec d={48000,bits+1,out,{},rate,{}};
 void*encoder=ec(&e);void*decoder=dc(&d);
 double energy=0;int sizes[513]={};struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
 for(int f=0;f<100;f++){
  for(int i=0;i<960;i++)in[i]=f<50?(int16_t)(6000*sin(2*3.141592653589793*600*(f*960+i)/48000.0)):0;
  int n=ef(encoder,&e);
  if(n<1||n>321){printf("FAIL frame bytes=%d toc=%u\n",n,bits[0]);return 3;}
  sizes[n]++;
  int ft=bits[0]&15, mode=(bits[0]>>5)&1;
  d.bitrate=mode?io[ft]:primary[ft]; d.more[2]=mode;d.more[3]=ft;
  d.options[2]=ft==14?1:0;
  df(decoder,&d);for(int i=0;i<960;i++)energy+=(double)out[i]*out[i];
 }
 clock_gettime(CLOCK_MONOTONIC,&end);
 double elapsed=(end.tv_sec-start.tv_sec)*1000.+(end.tv_nsec-start.tv_nsec)/1e6;
 ed(encoder);dd(decoder);
 printf("rate=%d bw=%d dtx=%d RMS=%.1f %.2f ms/frame sizes:",rate,bw,dtx,sqrt(energy/(100*960)),elapsed/100.);
 for(int i=1;i<=321;i++)if(sizes[i])printf(" %d:%d",i,sizes[i]);
 printf("\n");
 return energy>0?0:4;
}
