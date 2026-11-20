#include "iq_mixer.h"
#include <math.h>

void iq_mixer_init(iq_mixer_t *m) { *m=(iq_mixer_t){.re=1,.step_re=1}; }
void iq_mixer_set(iq_mixer_t *m, double shift_hz, uint32_t sample_rate)
{
    double angle=-6.2831853071795864769*shift_hz/sample_rate;
    m->step_re=(float)cos(angle); m->step_im=(float)sin(angle);
}
static int16_t quantize(float v,uint32_t *clipped)
{
    if(v>32767.f) { (*clipped)++;return 32767; }
    if(v< -32768.f) { (*clipped)++;return -32768; }
    return (int16_t)(v+(v>=0?0.5f:-0.5f));
}
static inline void process(iq_mixer_t *m,int16_t *samples,size_t count,int iq_order)
{
    float re=m->re,im=m->im;
    for(size_t i=0;i<count;i++) {
        float in=samples[2*i+(iq_order?0:1)],q=samples[2*i+(iq_order?1:0)];
        samples[2*i]=quantize(in*re-q*im,&m->clipped_components);
        samples[2*i+1]=quantize(in*im+q*re,&m->clipped_components);
        float next=re*m->step_re-im*m->step_im;
        im=re*m->step_im+im*m->step_re; re=next;
        /* Bound oscillator amplitude drift without a trigonometric call per sample. */
        if(++m->normalize==256) { float scale=1.f/sqrtf(re*re+im*im); re*=scale;im*=scale;m->normalize=0; }
    }
    m->re=re;m->im=im;
}
void iq_mixer_process(iq_mixer_t *m,int16_t *samples,size_t count) { process(m,samples,count,0); }
void iq_mixer_process_iq(iq_mixer_t *m,int16_t *samples,size_t count) { process(m,samples,count,1); }
