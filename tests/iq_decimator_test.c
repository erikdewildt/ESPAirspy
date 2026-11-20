#include "iq_decimator.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 262144u
static const double tau=6.2831853071795864769;
static void make_tone(int16_t *p,size_t n,double frequency)
{
    for(size_t i=0;i<n;i++){p[2*i]=(int16_t)lround(10000*cos(tau*frequency*i));p[2*i+1]=(int16_t)lround(10000*sin(tau*frequency*i));}
}
static double rms(const int16_t *p,size_t n)
{
    double energy=0;for(size_t i=256;i<n;i++)energy+=(double)p[2*i]*p[2*i]+(double)p[2*i+1]*p[2*i+1];
    return sqrt(energy/(n-256));
}
int main(void)
{
    int16_t *input=malloc(N*4),*whole=malloc(N*4),*fragmented=malloc(N*4);assert(input&&whole&&fragmented);
    for(unsigned stage=0;stage<=IQ_DECIMATOR_MAX_STAGES;stage++) {
        unsigned div=1u<<stage;iq_decimator_t d;
        make_tone(input,N,1.0/(32*div));memcpy(whole,input,N*4);iq_decimator_reset(&d,stage);
        size_t count=iq_decimator_process(&d,whole,N,1.f);assert(count==N/div);
        double amplitude=rms(whole,count);assert(fabs(amplitude-10000)<3);
        iq_decimator_reset(&d,stage);size_t out=0;
        for(size_t pos=0;pos<N;) {
            size_t len=123;if(len>N-pos)len=N-pos;
            int16_t block[246];memcpy(block,input+2*pos,len*4);
            size_t got=iq_decimator_process(&d,block,len,1.f);memcpy(fragmented+2*out,block,got*4);out+=got;pos+=len;
        }
        assert(out==count && !memcmp(whole,fragmented,out*4));
        if(stage) {
            make_tone(input,N,.375/(div/2));iq_decimator_reset(&d,stage);
            count=iq_decimator_process(&d,input,N,1.f);double leakage=rms(input,count);
            assert(leakage<2);printf("Stage %u: passband %.3f, alias RMS %.6f, output %zu, fragmented continuity exact\n",stage,amplitude,leakage,count);
        } else assert(!memcmp(whole,input,N*4));
    }
    iq_decimator_t gain; make_tone(input,N,1.0/(32*64));iq_decimator_reset(&gain,6);size_t scaled=iq_decimator_process(&gain,input,N,2.f);assert(fabs(rms(input,scaled)-20000)<5);
    iq_decimator_reset(&gain,0);int16_t v[]={1000,-1000,30000,-30000};assert(iq_decimator_process(&gain,v,2,2.f)==2);assert(v[0]==2000 && v[1]==-2000 && v[2]==32767 && v[3]==-32768);assert(gain.clipped_components==2);
    int16_t edge[]={32767,-32768};iq_decimator_process(&gain,edge,1,1.f);assert(gain.clipped_components==2);
    free(input);free(whole);free(fragmented);puts("IQ decimator tests passed");
}
