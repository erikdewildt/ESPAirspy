#include "dc_block.h"
#include "airspy_signal.h"

/* Pole of the first-order high-pass; see dc_block.h for the cutoff. */
#define DC_BLOCK_POLE 0.9999f

void dc_block_init(dc_block_t *d)
{
    d->x1_q = d->x1_i = 0.0f;
    d->y1_q = d->y1_i = 0.0f;
}


void dc_block_process(dc_block_t *d, int16_t *samples, size_t complex_count)
{
    float x1q = d->x1_q, x1i = d->x1_i;
    float y1q = d->y1_q, y1i = d->y1_i;
    for (size_t i = 0; i < complex_count; i++) {
        float q = (float)samples[2 * i];      /* Q */
        float in = (float)samples[2 * i + 1]; /* I */
        float yq = q - x1q + DC_BLOCK_POLE * y1q;
        float yi = in - x1i + DC_BLOCK_POLE * y1i;
        x1q = q; x1i = in;
        y1q = yq; y1i = yi;
        samples[2 * i] = airspy_quantize_i16(yq);
        samples[2 * i + 1] = airspy_quantize_i16(yi);
    }
    d->x1_q = x1q; d->x1_i = x1i;
    d->y1_q = y1q; d->y1_i = y1i;
}
