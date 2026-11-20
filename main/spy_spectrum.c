#include "spy_spectrum.h"
#include <math.h>
#include <string.h>
void spy_spectrum_init(spy_spectrum_t *s)
{
    memset(s,0,sizeof(*s));spy_fft_init(&s->fft);
}
bool spy_spectrum_configure(spy_spectrum_t *s,uint32_t rate,unsigned stage,uint32_t center)
{
    if(!s || rate<64 || stage>IQ_DECIMATOR_MAX_STAGES || (rate>>stage)==0)return false;
    s->input_rate=rate;s->output_rate=rate>>stage;s->stage=stage;s->center_hz=center;
    s->period=s->output_rate/10;if(s->period<SPY_FFT_SIZE)s->period=SPY_FFT_SIZE;
    s->filled=0;s->skip=0;iq_decimator_reset(&s->filter,stage);s->configured=true;return true;
}
static bool process(spy_spectrum_t *s,const int16_t *iq,size_t samples,uint32_t center,uint8_t *frame,float *frame_db,float offset,float range)
{
    if(!s || !s->configured || !iq || (!frame && !frame_db) || samples>SPY_SPECTRUM_CHUNK_MAX ||
       (frame && (!(range>0) || !isfinite(range) || !isfinite(offset))))return false;
    if(center!=s->center_hz && !spy_spectrum_configure(s,s->input_rate,s->stage,center))return false;
    bool ready=false;
    /* At native rate there is no filter state to maintain. Skip idle samples
     * in whole blocks and convert only those that actually enter an FFT. */
    if(!s->stage) {
        for(size_t pos=0;pos<samples;) {
            if(s->skip) {
                size_t n=samples-pos;if(n>s->skip)n=s->skip;
                s->skip-=n;pos+=n;continue;
            }
            size_t n=samples-pos;if(n>SPY_FFT_SIZE-s->filled)n=SPY_FFT_SIZE-s->filled;
            for(size_t i=0;i<2*n;i++)s->capture[2*s->filled+i]=(float)iq[2*pos+i];
            s->filled+=n;pos+=n;
            if(s->filled==SPY_FFT_SIZE) {
                if(frame_db)spy_fft_compute_f32_db(&s->fft,s->capture,frame_db);
                else if(frame){spy_fft_compute_f32(&s->fft,s->capture,s->full,offset,range);memcpy(frame,s->full,SPY_FFT_SIZE);}
                ready=true;s->filled=0;s->skip=s->period-SPY_FFT_SIZE;
            }
        }
        return ready;
    }
    for(size_t pos=0;pos<samples;) {
        size_t n=samples-pos;if(n>SPY_SPECTRUM_SCRATCH)n=SPY_SPECTRUM_SCRATCH;
        size_t count=iq_decimator_process_f32(&s->filter,iq+2*pos,s->scratch,n);pos+=n;
        for(size_t i=0;i<count;i++) {
            if(s->skip){s->skip--;continue;}
            s->capture[2*s->filled]=s->scratch[2*i];s->capture[2*s->filled+1]=s->scratch[2*i+1];s->filled++;
            if(s->filled==SPY_FFT_SIZE) {
                if(frame_db)spy_fft_compute_f32_db(&s->fft,s->capture,frame_db);
                else if(frame) {spy_fft_compute_f32(&s->fft,s->capture,s->full,offset,range);memcpy(frame,s->full,SPY_FFT_SIZE);}
                ready=true;s->filled=0;s->skip=s->period-SPY_FFT_SIZE;
            }
        }
    }
    return ready;
}
bool spy_spectrum_process(spy_spectrum_t *s,const int16_t *iq,size_t samples,uint32_t center,uint8_t *frame,float offset,float range)
{ return process(s,iq,samples,center,frame,NULL,offset,range); }
bool spy_spectrum_process_db(spy_spectrum_t *s,const int16_t *iq,size_t samples,uint32_t center,float *frame_db)
{ return process(s,iq,samples,center,NULL,frame_db,0,1); }
