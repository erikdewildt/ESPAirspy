/* Host test for the frequency-domain IQ balancer (main/iq_balance_freq.c).
 *
 * Verifies, on a tone with a known gain/phase imbalance:
 *   1. the estimator converges and the correction improves image rejection by
 *      far more than the hardware's uncorrected ~34 dB;
 *   2. the estimate is essentially immune to a large DC offset (which biases
 *      the static whole-signal estimator), because it correlates FFT bins
 *      with their images and skips the near-DC bins.
 *
 * Compile:
 *   cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined \
 *      -Imain tests/iq_balance_freq_test.c main/iq_balance_freq.c \
 *      main/airspy_signal.c -lm -o /tmp/iq_balance_freq_test
 */
#include "iq_balance_freq.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const double tau = 6.2831853071795864769;
#define RATE 768000.0
#define F 12000.0                 /* F/RATE = 1/64 (whole cycles) */
#define A 20000.0                 /* tone amplitude (int16 counts) */
#define G 1.02                    /* gain imbalance */
#define PHI (2.0 * tau / 360.0)   /* phase imbalance, 2 degrees */
#define WORKING (IQ_BAL_FREQ_FFT_BINS * 3)

/* Generate Q,I (wire order) int16 samples of an imbalanced tone. */
static void gen_tone(int16_t *s, size_t n, double g, double phi, double dc_i, double dc_q)
{
    for (size_t i = 0; i < n; i++) {
        double p = tau * F * i / RATE;
        double I = A * cos(p) + dc_i;
        double Q = g * (A * sin(p) * cos(phi) + A * cos(p) * sin(phi)) + dc_q;
        s[2 * i] = (int16_t)lround(Q);     /* Q */
        s[2 * i + 1] = (int16_t)lround(I); /* I */
    }
}

/* Image-rejection ratio (dB) of float I/Q data at +F vs -F (coherent). */
static double irr_float(const double *re, const double *im, size_t n)
{
    double pr = 0, pi = 0, nr = 0, ni = 0;
    for (size_t i = 0; i < n; i++) {
        double p = tau * F * i / RATE;
        double I = re[i], Q = im[i];
        pr += I * cos(p) + Q * sin(p);
        pi += Q * cos(p) - I * sin(p);
        double q = -p;
        nr += I * cos(q) + Q * sin(q);
        ni += Q * cos(q) - I * sin(q);
    }
    return 10.0 * log10((pr * pr + pi * pi) / (nr * nr + ni * ni));
}

/* Converge the estimator on a given imbalance by repeatedly filling the
 * working buffer with fresh imbalanced samples and estimating. */
static void converge(iq_balance_freq_t *b, double g, double phi, double dc_i, double dc_q)
{
    for (int iter = 0; iter < 60; iter++) {
        for (unsigned i = 0; i < WORKING; i++) {
            double p = tau * F * i / RATE;
            double I = A * cos(p) + dc_i;
            double Q = g * (A * sin(p) * cos(phi) + A * cos(p) * sin(phi)) + dc_q;
            b->working_buffer[i].re = (float)(I / 32768.0);
            b->working_buffer[i].im = (float)(Q / 32768.0);
        }
        iq_balance_freq_estimate(b);
    }
}

/* Image rejection after applying the converged correction to a fresh tone. */
static double irr_after(const iq_balance_freq_t *b, double g, double phi, size_t n)
{
    double *re = malloc(n * sizeof(double));
    double *im = malloc(n * sizeof(double));
    assert(re && im);
    for (size_t i = 0; i < n; i++) {
        double p = tau * F * i / RATE;
        double I = A * cos(p);
        double Q = g * (A * sin(p) * cos(phi) + A * cos(p) * sin(phi));
        double r = I / 32768.0, m = Q / 32768.0;
        double rc = r + (double)b->phase * m;
        double ic = m + (double)b->phase * r;
        re[i] = rc * (1.0 + (double)b->amplitude);
        im[i] = ic * (1.0 - (double)b->amplitude);
    }
    double r = irr_float(re, im, n);
    free(re); free(im);
    return r;
}

int main(void)
{
    size_t n = 65536;
    double before = irr_after(&(iq_balance_freq_t){0}, G, PHI, n); /* no correction */
    printf("Uncorrected image rejection: %.2f dB\n", before);
    assert(before < 40.0);

    /* Scenario 1: clean tone. */
    iq_balance_freq_t clean;
    iq_balance_freq_init(&clean);
    converge(&clean, G, PHI, 0, 0);
    printf("Clean tone -> phase %.6f amplitude %.6f\n",
           (double)clean.phase, (double)clean.amplitude);
    double after = irr_after(&clean, G, PHI, n);
    printf("Corrected image rejection: %.2f dB (improvement %.2f dB)\n", after, after - before);
    assert(after - before > 25.0);

    /* Scenario 2: same tone plus a large DC offset (3000 LSB) on both paths.
     * The estimate must stay essentially the same (DC-immune). */
    iq_balance_freq_t dc;
    iq_balance_freq_init(&dc);
    converge(&dc, G, PHI, 3000, 3000);
    printf("DC-biased tone -> phase %.6f amplitude %.6f\n",
           (double)dc.phase, (double)dc.amplitude);
    assert(fabs((double)dc.phase - (double)clean.phase) < 2e-3);
    assert(fabs((double)dc.amplitude - (double)clean.amplitude) < 2e-3);

    /* The per-sample process path must apply the correction without corrupting
     * the sample values (round-trip a balanced tone near full scale). */
    iq_balance_freq_t proc;
    iq_balance_freq_init(&proc);
    proc.phase = 0.0f; proc.last_phase = 0.0f;
    proc.amplitude = 0.0f; proc.last_amplitude = 0.0f;
    int16_t buf[1024 * 2];
    gen_tone(buf, 1024, 1.0, 0.0, 0, 0);
    for (int i = 0; i < 4; i++) iq_balance_freq_process(&proc, buf, 1024);
    /* A balanced tone with zero correction must stay a balanced tone. */
    double after_proc = 0;
    {
        double *re = malloc(1024 * sizeof(double));
        double *im = malloc(1024 * sizeof(double));
        assert(re && im);
        for (size_t i = 0; i < 1024; i++) {
            im[i] = buf[2 * i] / 32768.0;
            re[i] = buf[2 * i + 1] / 32768.0;
        }
        after_proc = irr_float(re, im, 1024);
        free(re); free(im);
    }
    printf("Balanced-tone process IR: %.2f dB\n", after_proc);
    assert(after_proc > 50.0);

    puts("iq_balance_freq: convergence, DC immunity and process path passed");
    return 0;
}
