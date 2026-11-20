/* Host test for the static IQ-imbalance corrector (main/iq_balance.c).
 *
 * Builds Q,I int16 samples with a known gain (2%) and phase (2 degree)
 * imbalance on the Q path, verifies the estimator recovers those values, and
 * measures the image-rejection improvement after correction. The correction
 * must recover image rejection by far more than the hardware ~34 dB.
 *
 * Compile:
 *   cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined \
 *      -Imain tests/iq_balance_test.c main/iq_balance.c -lm -o /tmp/iq_balance_test
 */
#include "iq_balance.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const double tau = 6.2831853071795864769;
#define RATE 768000.0
#define F 12000.0                 /* interferer; F/RATE = 1/64 (whole cycles) */
#define A 20000.0                 /* interferer amplitude */
#define G 1.02                    /* gain imbalance */
#define PHI (2.0 * tau / 360.0)   /* phase imbalance, 2 degrees */

/* Image-rejection ratio (dB) of Q,I interleaved data: power at +F vs -F. */
static double irr(const int16_t *s, size_t n)
{
    double pr = 0, pi = 0, nr = 0, ni = 0;
    for (size_t i = 0; i < n; i++) {
        double p = tau * F * i / RATE;
        double I = s[2 * i + 1], Q = s[2 * i]; /* complex sample = I + jQ */
        pr += I * cos(p) + Q * sin(p);
        pi += Q * cos(p) - I * sin(p);
        double q = -p;
        nr += I * cos(q) + Q * sin(q);
        ni += Q * cos(q) - I * sin(q);
    }
    return 10.0 * log10((pr * pr + pi * pi) / (nr * nr + ni * ni));
}

static void gen_tone(int16_t *s, size_t n, double g, double phi)
{
    for (size_t i = 0; i < n; i++) {
        double p = tau * F * i / RATE;
        double I = A * cos(p);
        double Q = g * (A * sin(p) * cos(phi) + A * cos(p) * sin(phi));
        s[2 * i] = (int16_t)lround(Q);     /* Q */
        s[2 * i + 1] = (int16_t)lround(I); /* I */
    }
}

int main(void)
{
    const size_t N = IQ_BALANCE_WINDOW * 2;
    int16_t *raw = malloc(N * 2 * sizeof(int16_t));
    assert(raw);

    gen_tone(raw, N, G, PHI);
    double before = irr(raw, IQ_BALANCE_WINDOW);
    printf("Before correction: image rejection %.2f dB\n", before);
    assert(before < 40.0); /* hardware imbalance is far below a clean signal */

    iq_balance_t b;
    iq_balance_init(&b);
    /* First window only estimates; the second is corrected in place. */
    for (size_t off = 0; off < N; off += 4096)
        iq_balance_process(&b, raw + 2 * off, 4096);

    assert(b.valid);
    printf("Estimated gain %.4f (true %.4f), sin(phi) %.4f (true %.4f)\n",
           (double)b.gain, G, (double)b.sin_phi, sin(PHI));
    assert(fabs((double)b.gain - G) < 0.01 * G);
    assert(fabs((double)b.sin_phi - sin(PHI)) < 0.01);

    double after = irr(raw + 2 * IQ_BALANCE_WINDOW, IQ_BALANCE_WINDOW);
    printf("After  correction: image rejection %.2f dB (improvement %.2f dB)\n",
           after, after - before);
    assert(after - before > 30.0);

    /* Silence must never enable correction. */
    iq_balance_init(&b);
    int16_t silence[512] = {0};
    for (int i = 0; i < 2048; i++) iq_balance_process(&b, silence, 256);
    assert(!b.valid);

    /* A clean (imbalance-free) signal must estimate near-zero imbalance. */
    {
        iq_balance_init(&b);
        size_t M = 262144; /* whole cycles at F/RATE = 1/64 */
        int16_t *clean = malloc(M * 2 * sizeof(int16_t));
        assert(clean);
        gen_tone(clean, M, 1.0, 0.0);
        for (size_t off = 0; off < M; off += 4096)
            iq_balance_process(&b, clean + 2 * off, 4096);
        iq_balance_update(&b); /* force from the accumulated stats */
        printf("Clean-signal estimate: gain %.5f, sin(phi) %.5f\n",
               (double)b.gain, (double)b.sin_phi);
        assert(fabs((double)b.gain - 1.0) < 0.01);
        assert(fabs((double)b.sin_phi) < 0.01);
        free(clean);
    }

    free(raw);
    puts("IQ balance: estimation, image rejection and edge cases passed");
    return 0;
}
