#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "spy_fft.h"
#include "iq_decimator.h"
#define SPY_SPECTRUM_CHUNK_MAX 4096u
#define SPY_SPECTRUM_SCRATCH 256u
typedef struct {
    spy_fft_t fft;iq_decimator_t filter;
    float capture[SPY_FFT_SIZE*2],scratch[SPY_SPECTRUM_SCRATCH*2];
    uint8_t full[SPY_FFT_SIZE];
    uint32_t input_rate,output_rate,center_hz,filled,skip,period;
    unsigned stage;bool configured;
} spy_spectrum_t;
void spy_spectrum_init(spy_spectrum_t *s);
bool spy_spectrum_configure(spy_spectrum_t *s,uint32_t input_rate,unsigned stage,uint32_t center_hz);
/* Consumes at most 4096 native-rate complex samples. Returns true when a
 * complete FFT-shifted frame is written; if several complete, frame contains
 * the latest one. Input and output must not alias the spectrum workspace. */
bool spy_spectrum_process(spy_spectrum_t *s,const int16_t *iq,size_t samples,uint32_t center_hz,uint8_t *frame,float offset_db,float range_db);
/* As above, but preserve 2048 dB values for a later client-specific window. */
bool spy_spectrum_process_db(spy_spectrum_t *s,const int16_t *iq,size_t samples,uint32_t center_hz,float *frame_db);
