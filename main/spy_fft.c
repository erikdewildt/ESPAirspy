#include "spy_fft.h"
#include <math.h>
#include <string.h>
#define PI_F 3.14159265358979323846f
void spy_fft_init(spy_fft_t *f)
{
    float sum=0;
    for(unsigned i=0;i<SPY_FFT_SIZE;i++) {
        f->window[i]=.5f-.5f*cosf(2.f*PI_F*i/(SPY_FFT_SIZE-1));sum+=f->window[i];
    }
    f->coherent_gain=sum;
    for(unsigned i=0;i<SPY_FFT_SIZE/2;i++) {
        float a=-2.f*PI_F*i/SPY_FFT_SIZE;f->twiddle_real[i]=cosf(a);f->twiddle_imag[i]=sinf(a);
    }
}
static inline void compute(spy_fft_t *f,const int16_t *iq16,const float *iqf,uint8_t *bins,float *bins_db,float offset,float range)
{
    if(bins && (!(range>0.f) || !isfinite(range) || !isfinite(offset))) { memset(bins,0,SPY_FFT_SIZE);return; }
    for(unsigned i=0,j=0;i<SPY_FFT_SIZE;i++) {
        float re=iqf?iqf[2*i]:(float)iq16[2*i],im=iqf?iqf[2*i+1]:(float)iq16[2*i+1];
        f->real[j]=re*f->window[i];f->imag[j]=im*f->window[i];
        unsigned bit=SPY_FFT_SIZE>>1;while(j&bit){j^=bit;bit>>=1;}j^=bit;
    }
    for(unsigned size=2;size<=SPY_FFT_SIZE;size<<=1) {
        unsigned half=size/2,stride=SPY_FFT_SIZE/size;
        for(unsigned base=0;base<SPY_FFT_SIZE;base+=size)for(unsigned j=0;j<half;j++) {
            unsigned a=base+j,b=a+half,k=j*stride;
            float wr=f->twiddle_real[k],wi=f->twiddle_imag[k];
            float re=wr*f->real[b]-wi*f->imag[b],im=wr*f->imag[b]+wi*f->real[b];
            float ar=f->real[a],ai=f->imag[a];
            f->real[a]=ar+re;f->imag[a]=ai+im;f->real[b]=ar-re;f->imag[b]=ai-im;
        }
    }
    float scale=1.f/(32768.f*f->coherent_gain);scale*=scale;
    for(unsigned i=0;i<SPY_FFT_SIZE;i++) {
        unsigned k=(i+SPY_FFT_SIZE/2)&(SPY_FFT_SIZE-1);
        float p=(f->real[k]*f->real[k]+f->imag[k]*f->imag[k])*scale;
        float db=p>0.f?10.f*log10f(p):-300.f;
        if(bins_db)bins_db[i]=db;
        if(bins) {
            float v=255.f*(db-offset+range)/range;
            bins[i]=v<=0?0:v>=255?255:(uint8_t)(v+.5f);
        }
    }
}
void spy_fft_compute(spy_fft_t *f,const int16_t *iq,uint8_t *bins,float offset,float range)
{ compute(f,iq,NULL,bins,NULL,offset,range); }
void spy_fft_compute_f32(spy_fft_t *f,const float *iq,uint8_t *bins,float offset,float range)
{ compute(f,NULL,iq,bins,NULL,offset,range); }
void spy_fft_compute_f32_db(spy_fft_t *f,const float *iq,float *bins_db)
{ compute(f,NULL,iq,NULL,bins_db,0,1); }
bool spy_fft_render(const uint8_t *full,uint8_t *out,size_t pixels,float rate,float center,float span)
{
    if(!full || !out || !pixels || pixels>4096 || !(rate>=1.f) || rate>1e9f || !(span>0.f) ||
       !isfinite(rate) || !isfinite(center) || !isfinite(span))return false;
    /* Bound the window to avoid overflow in index arithmetic. Windows may
     * extend beyond the FFT; that region is emitted at the display floor. */
    if(span>rate*2.f || fabsf(center)>rate*2.f)return false;
    float bin_hz=rate/SPY_FFT_SIZE;
    for(size_t i=0;i<pixels;i++) {
        float left=center+((float)i/(float)pixels-.5f)*span;
        float right=center+((float)(i+1)/(float)pixels-.5f)*span;
        float mid=(left+right)*.5f;
        float pos=mid/bin_hz+SPY_FFT_SIZE/2;
        uint8_t v=0;
        if(pos>=0.f && pos<SPY_FFT_SIZE) {
            unsigned j=(unsigned)pos;float fraction=pos-j;
            float a=full[j],b=full[j+1<SPY_FFT_SIZE?j+1:j];
            v=(uint8_t)(a+(b-a)*fraction+.5f);
        }
        /* When multiple source bins fall in a pixel, preserve narrow peaks. */
        int first=(int)ceilf(left/bin_hz+SPY_FFT_SIZE/2);
        int last=(int)ceilf(right/bin_hz+SPY_FFT_SIZE/2)-1;
        if(first<0)first=0;
        if(last>=(int)SPY_FFT_SIZE)last=SPY_FFT_SIZE-1;
        for(int j=first;j<=last;j++)if(full[j]>v)v=full[j];
        out[i]=v;
    }
    return true;
}
bool spy_fft_render_db(const float *full,uint8_t *out,size_t pixels,float rate,float center,float span,float offset,float range)
{
    if(!full || !out || !pixels || pixels>32768 || !(rate>=1.f) || rate>1e9f || !(span>0.f) ||
       !isfinite(rate) || !isfinite(center) || !isfinite(span) || !isfinite(offset) ||
       !isfinite(range) || range<.001f || range>1000.f || fabsf(offset)>1000.f ||
       span>rate*2.f || fabsf(center)>rate*2.f)return false;
    float bin_hz=rate/SPY_FFT_SIZE;
    for(size_t i=0;i<pixels;i++) {
        float left=center+((float)i/(float)pixels-.5f)*span;
        float right=center+((float)(i+1)/(float)pixels-.5f)*span;
        float pos=((left+right)*.5f)/bin_hz+SPY_FFT_SIZE/2;
        float db=offset-range;
        if(pos>=0.f && pos<SPY_FFT_SIZE) {
            unsigned j=(unsigned)pos;float fraction=pos-j;
            float a=full[j],b=full[j+1<SPY_FFT_SIZE?j+1:j];
            if(isfinite(a) && isfinite(b))db=a+(b-a)*fraction;
        }
        int first=(int)ceilf(left/bin_hz+SPY_FFT_SIZE/2);
        int last=(int)ceilf(right/bin_hz+SPY_FFT_SIZE/2)-1;
        if(first<0)first=0;
        if(last>=(int)SPY_FFT_SIZE)last=SPY_FFT_SIZE-1;
        for(int j=first;j<=last;j++)if(isfinite(full[j]) && full[j]>db)db=full[j];
        if(!isfinite(db))db=offset-range;
        float v=255.f*(db-offset+range)/range;
        out[i]=v<=0.f?0:v>=255.f?255:(uint8_t)(v+.5f);
    }
    return true;
}
