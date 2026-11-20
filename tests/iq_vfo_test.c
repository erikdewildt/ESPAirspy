#include "iq_mixer.h"
#include "iq_decimator.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 65536u
static const double tau=6.2831853071795864769;
static void run(double offset,int fused)
{
    int16_t *out=calloc(N*2,sizeof(*out));assert(out);
    iq_mixer_t mixer;iq_mixer_init(&mixer);iq_mixer_set(&mixer,offset,768000);
    iq_decimator_t decimator;iq_decimator_reset(&decimator,3);
    size_t produced=0;
    for(size_t pos=0;pos<N;) {
        size_t n=127;if(n>N-pos)n=N-pos;int16_t block[254];
        for(size_t j=0;j<n;j++) {
            double a=tau*(offset+12000)*(pos+j)/768000.;
            double b=tau*(offset+72000)*(pos+j)/768000.;
            block[2*j]=(int16_t)lround(4000*cos(a)+4000*cos(b));
            block[2*j+1]=(int16_t)lround(4000*sin(a)+4000*sin(b));
        }
        size_t got;
        if(fused)got=iq_decimator_process_mixed(&decimator,&mixer,block,n,1.f);
        else {iq_mixer_process_iq(&mixer,block,n);got=iq_decimator_process(&decimator,block,n,1.f);}
        memcpy(out+2*produced,block,got*4);produced+=got;pos+=n;
    }
    assert(produced==N/8 && mixer.clipped_components==0);
    double signal_re=0,signal_im=0,alias_re=0,alias_im=0;
    for(size_t i=128;i<produced;i++) {
        double re=out[2*i],im=out[2*i+1],a=tau*12000*i/96000.,b=tau*(-24000)*i/96000.;
        signal_re+=re*cos(a)+im*sin(a);signal_im+=im*cos(a)-re*sin(a);
        alias_re+=re*cos(b)+im*sin(b);alias_im+=im*cos(b)-re*sin(b);
    }
    double signal=hypot(signal_re,signal_im)/(produced-128),alias=hypot(alias_re,alias_im)/(produced-128);
    assert(fabs(signal-4000)<5);assert(alias<2.3);
    printf("offset %.0f Hz (%s): signal %.2f, rejected alias %.4f\n",offset,fused?"combined":"separate",signal,alias);
    free(out);
}
int main(void){for(int fused=0;fused<2;fused++){run(70000,fused);run(-70000,fused);run(300000,fused);run(-300000,fused);}puts("Independent I/Q tuning and decimation passed");}
