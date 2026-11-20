#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Static IQ-imbalance (image-rejection) correction.
 *
 * The Airspy HF+ zero/low-IF front end has a small gain/phase mismatch between
 * its I and Q paths. A strong signal in the image band then leaks into the
 * desired band and raises the noise floor. This module estimates the imbalance
 * from the streaming signal statistics and applies a Gram-Schmidt
 * orthogonalization, the same technique as the upstream libairspyhf adaptive
 * balancer, but with a periodic (rather than per-sample) coefficient update.
 *
 * Samples are the raw Airspy USB order: interleaved Q,I int16 pairs
 * (samples[2*i] = Q, samples[2*i+1] = I). The imbalance is modelled as gain g
 * and phase phi on the Q path; correction maps (Q,I) to (Q',I) with
 *     I  = I
 *     Q' = Q*b - I*a        where  b = 1/(g*cos(phi)), a = sin(phi)*b
 * so that the output Q is orthogonal to and equal-power with I.
 *
 * Estimation accumulates raw I^2, Q^2 and I*Q over a window; it assumes the
 * desired signal is "proper" (equal-power, uncorrelated I and Q), which holds
 * for broadband reception and for tones averaged over whole cycles. A strong
 * DC component can bias the estimate; this module does not remove DC.
 */
typedef struct {
    int64_t sum_i2, sum_q2, sum_iq; /* raw-sample accumulators */
    uint32_t count;                 /* samples in the current window */
    float a, b;                     /* correction coefficients */
    float gain;                     /* last estimated gain imbalance */
    float sin_phi;                  /* last estimated phase imbalance */
    bool valid;                     /* coefficients are usable */
} iq_balance_t;

/* Samples per estimation window (2^20 ~= 1.37 s at 768 kS/s). */
#define IQ_BALANCE_WINDOW 1048576u
/* Samples for the first estimate only (2^17 ~= 0.17 s at 768 kS/s), so image
 * rejection takes effect quickly; later windows refine with the larger size. */
#define IQ_BALANCE_FIRST_WINDOW 131072u

void iq_balance_init(iq_balance_t *b);
/* Correct Q,I int16 samples in place and accumulate imbalance statistics.
 * Coefficients are refreshed automatically once per window. */
void iq_balance_process(iq_balance_t *b, int16_t *samples, size_t complex_count);
/* Recompute coefficients from the accumulated statistics now (also called
 * internally at the window boundary). Exposed for tests and diagnostics. */
void iq_balance_update(iq_balance_t *b);
