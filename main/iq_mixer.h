#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct { float re, im, step_re, step_im; unsigned normalize; uint32_t clipped_components; } iq_mixer_t;
void iq_mixer_init(iq_mixer_t *m);
void iq_mixer_set(iq_mixer_t *m, double shift_hz, uint32_t sample_rate);
/* Airspy Q,I int16 input becomes SpyServer I,Q, shifted by -shift_hz. */
void iq_mixer_process(iq_mixer_t *m, int16_t *samples, size_t count);
/* Shift samples already in I,Q order without exchanging their components. */
void iq_mixer_process_iq(iq_mixer_t *m, int16_t *samples, size_t count);
