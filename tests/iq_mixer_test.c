#include "iq_mixer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void tone(double shift)
{
    const double tau=6.2831853071795864769, rate=768000, tone_hz=shift+17000;
    iq_mixer_t m; iq_mixer_init(&m); iq_mixer_set(&m,shift,(uint32_t)rate);
    /* Multiple blocks exercise oscillator continuity and long-term amplitude drift. */
    unsigned long index=0;
    for(unsigned block=0;block<1000;block++) {
        int16_t data[512];
        for(unsigned j=0;j<256;j++) {
            double phase=tau*tone_hz*(index+j)/rate;
            data[2*j]=(int16_t)lround(10000*sin(phase));
            data[2*j+1]=(int16_t)lround(10000*cos(phase));
        }
        iq_mixer_process(&m,data,256);
        for(unsigned j=0;j<256;j++) {
            double phase=tau*17000*(index+j)/rate;
            assert(fabs(data[2*j]-10000*cos(phase))<12);
            assert(fabs(data[2*j+1]-10000*sin(phase))<12);
        }
        index+=256;
    }
}
int main(void)
{
    tone(0); tone(123);tone(-499);tone(-179500);tone(420.25);
    iq_mixer_t m; iq_mixer_init(&m);iq_mixer_set(&m,0,768000);
    int16_t edge[]={-32768,32767,32767,-32768};iq_mixer_process(&m,edge,2);
    assert(edge[0]==32767 && edge[1]==-32768 && edge[2]==-32768 && edge[3]==32767);
    assert(m.clipped_components==0);
    iq_mixer_init(&m);int16_t already_iq[]={32767,-32768,-123,456};
    iq_mixer_process_iq(&m,already_iq,2);
    assert(already_iq[0]==32767 && already_iq[1]==-32768 && already_iq[2]==-123 && already_iq[3]==456);
    assert(m.clipped_components==0);
    iq_mixer_init(&m);m.re=.70710678f;m.im=.70710678f;
    int16_t strong[]={30000,30000};iq_mixer_process(&m,strong,1);
    assert(strong[0]==0 && strong[1]==32767 && m.clipped_components==1);
    puts("IQ mixer: direction, sub-kHz offsets, LF offset, continuity and amplitude passed");
}
