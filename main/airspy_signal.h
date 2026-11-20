#pragma once
#include <stddef.h>
#include <stdint.h>
#include <math.h>
/* Number of complex input samples with either component near a 16-bit rail. */
uint32_t airspy_iq_near_rail(const int16_t *iq,size_t samples);

/* Saturate finite filter output, then round halfway values away from zero.
 * On the P4 min/max and sign injection compile to branch-free FPU instructions. */
static inline int16_t airspy_quantize_i16(float value)
{
    float limited = fmaxf(-32768.f, fminf(32767.f, value));
    return (int16_t)(limited + copysignf(.5f, limited));
}
