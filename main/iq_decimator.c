#include "iq_decimator.h"
#include <math.h>
#include <string.h>

/* 55-tap Kaiser halfband: 0..0.2 Fs ripple <0.002 dB and
 * 0.3..0.5 Fs attenuation >79 dB before output quantization. */
static const float pairs[14]={
    -1.57092169605e-05f,0.000101469071921f,-0.000323663600258f,0.000791756793547f,
    -0.00165988984304f,0.00313374474804f,-0.00548159379354f,0.00906027718811f,
    -0.0143824706161f,0.0222960758909f,-0.0345027795883f,0.0553594681878f,
    -0.10092350967f,0.316546824448f
};
void iq_decimator_reset(iq_decimator_t *d,unsigned stages)
{
    memset(d,0,sizeof(*d));d->stages=stages>IQ_DECIMATOR_MAX_STAGES?IQ_DECIMATOR_MAX_STAGES:stages;
}
static int16_t quantize(float v,uint32_t *clipped)
{
    if(v>32767.f){ (*clipped)++;return 32767; }
    if(v< -32768.f){ (*clipped)++;return -32768; }
    return (int16_t)(v+(v>=0?0.5f:-0.5f));
}
static inline int feed(iq_decimator_t *d,float *input_re,float *input_im)
{
    float re=*input_re,im=*input_im;unsigned stage;
    for(stage=0;stage<d->stages;stage++) {
            iq_halfband_t *h=&d->filters[stage];unsigned pos=h->pos;
            h->re[pos]=h->re[pos+64]=re;h->im[pos]=h->im[pos+64]=im;h->pos=(pos+1)&63u;h->phase^=1;
            if(h->phase)return 0;
            /* Both halves of the delay line mirror the last 64 samples.
             * Index from the newest copy so each tap is a constant offset. */
            const float *recent_re = &h->re[pos + 64];
            const float *recent_im = &h->im[pos + 64];
            re = .5f * recent_re[-27];
            im = .5f * recent_im[-27];
            for (int k = 0; k < 14; k++) {
                int a = -2 * k;
                int b = -54 + 2 * k;
                re += pairs[k] * (recent_re[a] + recent_re[b]);
                im += pairs[k] * (recent_im[a] + recent_im[b]);
            }
    }
    *input_re=re;*input_im=im;return 1;
}
size_t iq_decimator_process(iq_decimator_t *d,int16_t *iq,size_t samples,float gain)
{
    if(!d->stages) { if(gain!=1.f)for(size_t i=0;i<2*samples;i++)iq[i]=quantize(iq[i]*gain,&d->clipped_components);return samples; }
    size_t out=0;
    for(size_t i=0;i<samples;i++) {
        float re=iq[2*i],im=iq[2*i+1];
        if(feed(d,&re,&im)) { iq[2*out]=quantize(re*gain,&d->clipped_components);iq[2*out+1]=quantize(im*gain,&d->clipped_components);out++; }
    }
    return out;
}
size_t iq_decimator_process_f32(iq_decimator_t *d,const int16_t *iq,float *out,size_t samples)
{
    size_t count=0;
    for(size_t i=0;i<samples;i++) {
        float re=iq[2*i],im=iq[2*i+1];
        if(feed(d,&re,&im)) { out[2*count]=re;out[2*count+1]=im;count++; }
    }
    return count;
}
size_t iq_decimator_process_mixed(iq_decimator_t *d,iq_mixer_t *m,int16_t *iq,size_t samples,float gain)
{
    size_t out=0;float osc_re=m->re,osc_im=m->im;
    for(size_t i=0;i<samples;i++) {
        float in_re=iq[2*i],in_im=iq[2*i+1];
        float re=in_re*osc_re-in_im*osc_im,im=in_re*osc_im+in_im*osc_re;
        float next=osc_re*m->step_re-osc_im*m->step_im;
        osc_im=osc_re*m->step_im+osc_im*m->step_re;osc_re=next;
        if(++m->normalize==256) {float scale=1.f/sqrtf(osc_re*osc_re+osc_im*osc_im);osc_re*=scale;osc_im*=scale;m->normalize=0;}
        if(feed(d,&re,&im)) {iq[2*out]=quantize(re*gain,&d->clipped_components);iq[2*out+1]=quantize(im*gain,&d->clipped_components);out++;}
    }
    m->re=osc_re;m->im=osc_im;return out;
}
