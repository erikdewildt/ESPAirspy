#pragma once
#include <stddef.h>
#include <stdint.h>
#include "iq_mixer.h"
#define IQ_DECIMATOR_MAX_STAGES 6u
typedef struct { float re[128],im[128]; unsigned pos,phase; } iq_halfband_t;
typedef struct { iq_halfband_t filters[IQ_DECIMATOR_MAX_STAGES]; unsigned stages; uint32_t clipped_components; } iq_decimator_t;
void iq_decimator_reset(iq_decimator_t *d,unsigned stages);
/* In-place I,Q int16 input; returns the number of output complex samples. */
size_t iq_decimator_process(iq_decimator_t *d,int16_t *iq,size_t samples,float gain);
/* Mix and filter in one pass, retaining float precision until final output. */
size_t iq_decimator_process_mixed(iq_decimator_t *d,iq_mixer_t *m,int16_t *iq,size_t samples,float gain);
/* Float output retains sub-LSB results through later spectral averaging.
 * out must hold at least 2*samples floats and not overlap iq. */
size_t iq_decimator_process_f32(iq_decimator_t *d,const int16_t *iq,float *out,size_t samples);
