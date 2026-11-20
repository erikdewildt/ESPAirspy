#include "iq_balance.h"
#include "airspy_signal.h"
#include <math.h>

/* Guard against an estimate produced from silence, a DC-only input, or an
 * unphysical imbalance. Values outside these bounds keep the previous
 * coefficients instead of applying a nonsense correction. */
#define IQ_BALANCE_MIN_GAIN 0.5f
#define IQ_BALANCE_MAX_GAIN 2.0f
#define IQ_BALANCE_MAX_SIN_PHI 0.5f

void iq_balance_init(iq_balance_t *b)
{
    b->sum_i2 = b->sum_q2 = b->sum_iq = 0;
    b->count = 0;
    b->a = 0.0f;
    b->b = 1.0f;
    b->gain = 1.0f;
    b->sin_phi = 0.0f;
    b->valid = false;
}


void iq_balance_update(iq_balance_t *b)
{
    if (b->count == 0 || b->sum_i2 <= 0 || b->sum_q2 <= 0) return;
    double ii = (double)b->sum_i2, qq = (double)b->sum_q2, iq = (double)b->sum_iq;
    double gain = sqrt(qq / ii);
    double sin_phi = iq / sqrt(ii * qq);
    if (!(gain >= IQ_BALANCE_MIN_GAIN && gain <= IQ_BALANCE_MAX_GAIN) ||
        !(sin_phi >= -IQ_BALANCE_MAX_SIN_PHI && sin_phi <= IQ_BALANCE_MAX_SIN_PHI))
        return; /* implausible estimate; keep the previous coefficients */
    double cos_phi = sqrt(1.0 - sin_phi * sin_phi);
    double inv = 1.0 / (gain * cos_phi);
    b->gain = (float)gain;
    b->sin_phi = (float)sin_phi;
    b->a = (float)(sin_phi * inv);
    b->b = (float)inv;
    b->valid = true;
}

void iq_balance_process(iq_balance_t *b, int16_t *samples, size_t complex_count)
{
    for (size_t i = 0; i < complex_count; i++) {
        int32_t q = samples[2 * i];       /* Q (quadrature) */
        int32_t in = samples[2 * i + 1];  /* I (in-phase) */
        b->sum_i2 += in * in;
        b->sum_q2 += q * q;
        b->sum_iq += in * q;
        b->count++;
        if (b->valid)
            samples[2 * i] = airspy_quantize_i16((float)q * b->b - (float)in * b->a);
        /* samples[2*i+1] (I) is left unchanged */
    }
    if (b->count >= (b->valid ? IQ_BALANCE_WINDOW : IQ_BALANCE_FIRST_WINDOW)) {
        iq_balance_update(b);
        b->sum_i2 = b->sum_q2 = b->sum_iq = 0;
        b->count = 0;
    }
}
