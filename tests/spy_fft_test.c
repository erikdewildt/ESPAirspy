#include "spy_fft.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static void test_tone(spy_fft_t *f,int16_t *iq,uint8_t *bins,int tone)
{
    const double pi=3.141592653589793;
    for(unsigned i=0;i<SPY_FFT_SIZE;i++){double a=2*pi*tone*i/SPY_FFT_SIZE;iq[2*i]=(int16_t)lround(8192*cos(a));iq[2*i+1]=(int16_t)lround(8192*sin(a));}
    spy_fft_compute(f,iq,bins,0,150);
    unsigned peak=0;for(unsigned i=1;i<SPY_FFT_SIZE;i++)if(bins[i]>bins[peak])peak=i;
    unsigned expected=(unsigned)((int)SPY_FFT_SIZE/2+tone);
    assert(peak==expected);
    double db=-150.0*(1.0-bins[peak]/255.0);assert(fabs(db-20*log10(.25))<.5);
    unsigned opposite=(unsigned)((int)SPY_FFT_SIZE/2-tone);if(tone)assert(bins[opposite]<70);
}
static void test_render(void)
{
    uint8_t *source=calloc(SPY_FFT_SIZE,1),out[512];assert(source);
    source[SPY_FFT_SIZE/2+123]=200;
    assert(spy_fft_render(source,out,512,768000,0,660000));
    unsigned peak=0;for(unsigned i=1;i<512;i++)if(out[i]>out[peak])peak=i;
    float hz=((peak+.5f)/512.f-.5f)*660000;
    assert(fabsf(hz-123*375.f)<660000.f/512.f);
    assert(out[peak]==200); /* max-pooling must not lose a narrow carrier */
    assert(spy_fft_render(source,out,512,768000,10000,120000));
    peak=0;for(unsigned i=1;i<512;i++)if(out[i]>out[peak])peak=i;
    hz=10000+((peak+.5f)/512.f-.5f)*120000;
    assert(fabsf(hz-123*375.f)<500);
    assert(!spy_fft_render(source,out,512,0,0,660000));
    assert(!spy_fft_render(source,out,512,1e-40f,0,1e-40f));
    assert(!spy_fft_render(source,out,512,1e30f,0,1e30f));
    assert(!spy_fft_render(source,out,0,768000,0,660000));
    for(int center=-700000;center<=700000;center+=70000)
        for(int span=1000;span<=1000000;span+=99900)
            assert(spy_fft_render(source,out,512,768000,(float)center,(float)span));
    free(source);
}
static void test_float_db(spy_fft_t *f)
{
    float *iq=calloc(SPY_FFT_SIZE*2,sizeof(*iq)),*db=calloc(SPY_FFT_SIZE,sizeof(*db));
    uint8_t out[512];assert(iq&&db);
    for(unsigned i=0;i<SPY_FFT_SIZE;i++) {
        double a=2*3.141592653589793*123*i/SPY_FFT_SIZE;
        iq[2*i]=(float)(8192*cos(a));iq[2*i+1]=(float)(8192*sin(a));
    }
    spy_fft_compute_f32_db(f,iq,db);
    assert(fabsf(db[SPY_FFT_SIZE/2+123]-(float)(20*log10(.25)))<.01f);
    for(unsigned i=0;i<SPY_FFT_SIZE;i++)db[i]=-240.f;
    assert(spy_fft_render_db(db,out,512,768000,0,660000,-100,150));
    for(unsigned i=0;i<512;i++)assert(out[i]==17);
    db[SPY_FFT_SIZE/2+123]=-20.f;
    assert(spy_fft_render_db(db,out,512,768000,0,660000,0,100));
    unsigned peak=0;for(unsigned i=1;i<512;i++)if(out[i]>out[peak])peak=i;
    assert(out[peak]==204);
    assert(fabsf(((peak+.5f)/512.f-.5f)*660000.f-123*375.f)<660000.f/512.f);
    assert(!spy_fft_render_db(db,out,512,768000,0,660000,0,0));
    assert(!spy_fft_render_db(db,out,512,768000,0,660000,INFINITY,150));
    assert(!spy_fft_render_db(db,out,32769,768000,0,660000,0,150));
    uint8_t *wide=malloc(32768);assert(wide);
    assert(spy_fft_render_db(db,wide,32768,768000,0,660000,-100,150));
    assert(wide[0]==17 && wide[32767]==17);
    free(wide);
    free(iq);free(db);
}
int main(void)
{
    spy_fft_t *f=calloc(1,sizeof(*f));int16_t *iq=calloc(SPY_FFT_SIZE*2,sizeof(*iq));uint8_t *bins=calloc(SPY_FFT_SIZE,1);assert(f&&iq&&bins);
    spy_fft_init(f);spy_fft_compute(f,iq,bins,0,150);for(unsigned i=0;i<SPY_FFT_SIZE;i++)assert(bins[i]==0);
    test_tone(f,iq,bins,123);test_tone(f,iq,bins,-345);test_tone(f,iq,bins,0);test_tone(f,iq,bins,1000);test_tone(f,iq,bins,-1000);
    spy_fft_compute(f,iq,bins,0,0);for(unsigned i=0;i<SPY_FFT_SIZE;i++)assert(bins[i]==0);
    test_render();
    test_float_db(f);
    free(f);free(iq);free(bins);puts("SpyServer FFT prototype: silence, positive/negative frequency, DC and dB scaling passed");
}
