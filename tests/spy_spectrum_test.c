#include "spy_spectrum.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define N 140000u
static void tone_test(unsigned stage)
{
    spy_spectrum_t *s=calloc(1,sizeof(*s)),*sd=calloc(1,sizeof(*sd));
    uint8_t frame[SPY_FFT_SIZE];float db_frame[SPY_FFT_SIZE];assert(s&&sd);
    spy_spectrum_init(s);assert(spy_spectrum_configure(s,768000,stage,7074000));
    spy_spectrum_init(sd);assert(spy_spectrum_configure(sd,768000,stage,7074000));
    double sample_rate=768000./(1u<<stage),tone=sample_rate*123/2048.,tau=6.2831853071795864769;
    unsigned frames=0;
    for(size_t pos=0;pos<N;) {
        size_t n=127;if(n>N-pos)n=N-pos;int16_t in[254];
        for(size_t j=0;j<n;j++){double a=tau*tone*(pos+j)/768000.;in[2*j]=(int16_t)lround(8192*cos(a));in[2*j+1]=(int16_t)lround(8192*sin(a));}
        bool bytes_ready=spy_spectrum_process(s,in,n,7074000,frame,0,150);
        bool db_ready=spy_spectrum_process_db(sd,in,n,7074000,db_frame);
        assert(bytes_ready==db_ready);
        if(bytes_ready) {
            unsigned peak=0;for(unsigned k=1;k<SPY_FFT_SIZE;k++)if(frame[k]>frame[peak])peak=k;
            assert(peak==SPY_FFT_SIZE/2+123);
            double db=-150*(1-frame[peak]/255.);assert(fabs(db-20*log10(.25))<.8);frames++;
            assert(fabs(db_frame[peak]-20*log10(.25))<.25);
        }
        pos+=n;
    }
    assert(frames>0);printf("stage %u: %u FFT frame(s), sample rate %.0f, tone %.2f Hz\n",stage,frames,sample_rate,tone);
    free(s);free(sd);
}
static void fractional_test(void)
{
    int16_t in[8192];float out[8192];uint32_t x=1;for(unsigned i=0;i<8192;i++){x=x*1664525u+1013904223u;in[i]=(x>>31)?1:-1;}
    iq_decimator_t d;iq_decimator_reset(&d,6);size_t n=iq_decimator_process_f32(&d,in,out,4096);assert(n==64);
    unsigned fractional=0;for(size_t i=0;i<2*n;i++)if(fabsf(out[i])>.001f && fabsf(out[i])<.49f)fractional++;
    assert(fractional>20);
}
static void native_cadence_test(void)
{
    enum { COUNT=170000, PERIOD=76800 };
    int16_t *input=malloc(COUNT*2*sizeof(*input));spy_spectrum_t *s=calloc(1,sizeof(*s));
    spy_fft_t *reference=calloc(1,sizeof(*reference));assert(input&&s&&reference);
    uint32_t random=123;
    for(size_t i=0;i<COUNT*2;i++){random=random*1664525u+1013904223u;input[i]=(int16_t)(random>>16);}
    spy_spectrum_init(s);spy_fft_init(reference);assert(spy_spectrum_configure(s,768000,0,7074000));
    uint8_t actual[SPY_FFT_SIZE],expected[SPY_FFT_SIZE];unsigned frames=0;
    for(size_t pos=0;pos<COUNT;) {
        size_t n=4093;if(n>COUNT-pos)n=COUNT-pos;
        if(spy_spectrum_process(s,input+2*pos,n,7074000,actual,0,150)) {
            size_t last_start=((pos+n-SPY_FFT_SIZE)/PERIOD)*PERIOD;
            spy_fft_compute(reference,input+2*last_start,expected,0,150);
            for(size_t i=0;i<SPY_FFT_SIZE;i++)assert(abs((int)actual[i]-(int)expected[i])<=1);
            frames++;
        }
        pos+=n;
    }
    assert(frames==3);free(reference);free(s);free(input);
}
int main(void)
{
    fractional_test();native_cadence_test();for(unsigned i=0;i<=IQ_DECIMATOR_MAX_STAGES;i++)tone_test(i);
    spy_spectrum_t *s=calloc(1,sizeof(*s));assert(s);spy_spectrum_init(s);
    assert(!spy_spectrum_configure(NULL,768000,0,7074000));
    assert(!spy_spectrum_configure(s,768000,7,7074000));assert(!spy_spectrum_configure(s,0,0,7074000));
    assert(spy_spectrum_configure(s,768000,0,7074000));
    int16_t silence[2048]={0};uint8_t frame[SPY_FFT_SIZE];
    assert(!spy_spectrum_process(s,silence,1024,7074000,frame,0,150));
    assert(!spy_spectrum_process(s,silence,1024,7100000,frame,0,150));
    assert(s->filled==1024 && s->center_hz==7100000);
    assert(spy_spectrum_process(s,silence,1024,7100000,frame,0,150));
    free(s);puts("Spectrum preprocessing: all zoom stages, sample continuity and sub-LSB precision passed");
}
