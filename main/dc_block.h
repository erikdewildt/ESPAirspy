#pragma once
#include <stddef.h>
#include <stdint.h>

/* First-order DC blocker (high-pass) for the Airspy HF+ baseband.
 *
 * The HF+ zero/low-IF front end leaks the LO into the baseband, producing a DC
 * spike at the passband center that masks weak signals within a few Hz of
 * center and biases an IQ-imbalance estimate. This module removes the DC (and
 * very low frequencies) with a first-order high-pass:
 *
 *     y[n] = x[n] - x[n-1] + R * y[n-1]
 *
 * with R close to 1, giving a -3 dB cutoff near (1-R)*Fs/(2*pi). For
 * R = 0.9999 at 768 kS/s the cutoff is about 12 Hz; signals above ~50 Hz pass
 * essentially unchanged.
 *
 * Samples are the raw Airspy USB order: interleaved Q,I int16 pairs. The filter
 * runs on Q and I independently and keeps its state across blocks.
 */
typedef struct {
    float x1_q, x1_i; /* previous input */
    float y1_q, y1_i; /* previous output (filter state) */
} dc_block_t;

void dc_block_init(dc_block_t *d);
void dc_block_process(dc_block_t *d, int16_t *samples, size_t complex_count);
