/* Host test for the DC blocker (main/dc_block.c).
 *
 * Verifies a large DC offset is removed, a weak tone near DC survives the
 * blocker, and that removing DC first un-biases an IQ-imbalance estimate (a DC
 * offset would otherwise corrupt E[I*Q] / E[Q^2]).
 *
 * Compile:
 *   cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined \
 *      -Imain tests/dc_block_test.c main/dc_block.c main/iq_balance.c -lm \
 *      -o /tmp/dc_block_test
 */
#include "dc_block.h"
#include "iq_balance.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const double tau = 6.2831853071795864769;
#define RATE 768000u
#define N 768000u    /* 1 second */
#define SETTLE 60000u /* skip the blocker transient (6 time constants) */
#define F 100.0

static double component_mean(const int16_t *s, size_t n)
{
    double m = 0;
    for (size_t i = 0; i < n; i++) m += s[2 * i]; /* Q component */
    return m / n;
}

static double tone_amp(const int16_t *s, size_t n)
{
    double re = 0, im = 0;
    for (size_t i = 0; i < n; i++) {
        double p = tau * F * i / RATE;
        double I = s[2 * i + 1], Q = s[2 * i];
        re += I * cos(p) + Q * sin(p);
        im += Q * cos(p) - I * sin(p);
    }
    return hypot(re, im) / n;
}

int main(void)
{
    int16_t *buf = malloc(N * 2 * sizeof(int16_t));
    assert(buf);
    dc_block_t d;

    /* 1. A constant DC offset (no tone) must be removed after settling. */
    for (size_t i = 0; i < N; i++) { buf[2 * i] = 3000; buf[2 * i + 1] = -3000; }
    dc_block_init(&d);
    for (size_t off = 0; off < N; ) {
        size_t len = 4096; if (len > N - off) len = N - off;
        dc_block_process(&d, buf + 2 * off, len); off += len;
    }
    double dc_after = component_mean(buf + 2 * SETTLE, N - SETTLE);
    printf("DC offset: 3000 LSB -> blocked mean %.4f LSB\n", dc_after);
    assert(fabs(dc_after) < 1.0);

    /* 2. A clean weak tone near DC must pass essentially unchanged. */
    for (size_t i = 0; i < N; i++) {
        double p = tau * F * i / RATE;
        buf[2 * i] = (int16_t)lround(4.0 * sin(p));
        buf[2 * i + 1] = (int16_t)lround(4.0 * cos(p));
    }
    dc_block_init(&d);
    for (size_t off = 0; off < N; ) {
        size_t len = 4096; if (len > N - off) len = N - off;
        dc_block_process(&d, buf + 2 * off, len); off += len;
    }
    double amp = tone_amp(buf + 2 * SETTLE, N - SETTLE);
    printf("Weak tone @ %.0f Hz amplitude: %.3f LSB (true 4.00)\n", F, amp);
    assert(fabs(amp - 4.0) < 0.3);

    /* 3. Blocking DC first un-biases the IQ-imbalance estimate. */
    {
        const double G = 1.02, PHI = 2.0 * tau / 360.0;
        iq_balance_t b;
        iq_balance_init(&b);
        dc_block_init(&d);
        for (size_t i = 0; i < N; i++) {
            double p = tau * F * i / RATE;
            double I = 20000.0 * cos(p);
            double Q = G * (20000.0 * sin(p) * cos(PHI) + 20000.0 * cos(p) * sin(PHI));
            int16_t pair[2] = { (int16_t)lround(Q + 3000.0), (int16_t)lround(I + 3000.0) };
            dc_block_process(&d, pair, 1);
            iq_balance_process(&b, pair, 1);
        }
        iq_balance_update(&b);
        printf("DC removed first: balance gain %.4f (true %.4f), sin(phi) %.4f (true %.4f)\n",
               (double)b.gain, G, (double)b.sin_phi, sin(PHI));
        assert(fabs((double)b.gain - G) < 0.02 * G);
        assert(fabs((double)b.sin_phi - sin(PHI)) < 0.01);
    }

    /* 4. Silence must stay silent. */
    dc_block_init(&d);
    int16_t zero[512] = {0};
    for (int i = 0; i < 1024; i++) dc_block_process(&d, zero, 256);
    for (int i = 0; i < 512; i++) assert(zero[i] == 0);

    free(buf);
    puts("DC blocker: offset removal, near-DC tone, balance de-bias and silence passed");
    return 0;
}
