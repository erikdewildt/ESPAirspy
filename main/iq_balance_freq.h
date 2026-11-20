#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/* Frequency-domain adaptive IQ-imbalance (image-rejection) correction.
 *
 * This is a CPU-adapted port of the upstream libairspyhf iqbalancer.c
 * (Copyright 2016-2023 Youssef Touil, 2018 Leif Asbrink, BSD-3-Clause-style). Unlike the
 * static whole-signal Gram-Schmidt corrector in iq_balance.c, this module
 * estimates the imbalance in the frequency domain:
 *
 *   - it cancels DC with a slow first-order running average before anything
 *     else, so the LO-leak DC spike cannot bias the estimate;
 *   - it correlates each FFT bin with its image bin (which measures actual
 *     image leakage, not whole-signal I/Q statistics), so a strong or
 *     asymmetric received spectrum does not bias the estimate;
 *   - it adapts phase/amplitude with a perturbed line search, weighting the
 *     correlation toward a chosen "optimal point" so the correction can be
 *     prioritized at the frequency of interest.
 *
 * Samples are the raw Airspy USB order: interleaved Q,I int16 pairs
 * (samples[2*i] = Q, samples[2*i+1] = I). The correction applied per sample is
 *
 *     I' = (I + phase*Q) * (1 + amplitude)
 *     Q' = (Q + phase*I) * (1 - amplitude)
 *
 * which is the upstream symmetric form (both I and Q are adjusted), computed
 * in float and quantized back to int16 like the rest of the pipeline.
 *
 * The FFT-based estimation is throttled to a few estimates per second (the
 * imbalance is quasi-static), keeping the added core load around a percent;
 * the per-sample correction itself is a few multiply-adds.
 */

/* FFT length. 512 bins = ~1.5 kHz/bin at 768 kS/s, sufficient for the slowly
 * varying HF+ zero-IF I/Q imbalance while keeping the working set ~26 KiB. */
#define IQ_BAL_FREQ_FFT_BINS 512u

typedef struct { float re, im; } iq_balance_freq_complex_t;

typedef struct {
    /* Correction state (phase/amplitude parametrization, as upstream).
     * The estimation task writes these and the streaming callback reads them;
     * 32-bit aligned float loads/stores are atomic on the target, so no lock is
     * needed on the hot path. */
    volatile float phase, last_phase;
    volatile float amplitude, last_amplitude;
    /* DC cancel state (slow running average, DcTimeConst per sample). */
    float iavg, qavg;
    /* Estimation accumulators. */
    float integrated_total_power, integrated_image_power, maximum_image_power;
    float raw_phases[4], raw_amplitudes[4]; /* MaxLookback = 4 */
    int skipped_buffers;
    int working_buffer_pos;
    int no_of_avg, no_of_raw, raw_ptr;
    int optimal_bin;
    int reset_flag;
    int fills_since_estimate;
    int power_flag[2]; /* FFTIntegration = 2 */
    /* Caller-owned buffers (no heap allocation in the hot path). */
    iq_balance_freq_complex_t corr[IQ_BAL_FREQ_FFT_BINS];
    iq_balance_freq_complex_t corr_plus[IQ_BAL_FREQ_FFT_BINS];
    iq_balance_freq_complex_t working_buffer[IQ_BAL_FREQ_FFT_BINS * 3]; /* producer */
    iq_balance_freq_complex_t ready_buffer[IQ_BAL_FREQ_FFT_BINS * 3];   /* estimator task */
    iq_balance_freq_complex_t scratch[IQ_BAL_FREQ_FFT_BINS];
    float boost[IQ_BAL_FREQ_FFT_BINS];
    atomic_bool handoff_busy; /* producer set, task clears */
    atomic_bool reset_pending;/* producer set, task honours */
    void *task;               /* TaskHandle_t on the target; unused by host tests */
} iq_balance_freq_t;

void iq_balance_freq_init(iq_balance_freq_t *b);
/* Producer-side reset (streaming callback, e.g. when the DSP is toggled):
 * resets the filter/DC state and asks the estimation task to restart. It does
 * not clear the task handle or its buffers, so it is safe while a task runs.
 * On a host build (no ESP_PLATFORM) the estimation runs inline. */
void iq_balance_freq_reset(iq_balance_freq_t *b);
/* Correct Q,I int16 samples in place (DC cancel + phase/amplitude correction)
 * and feed the estimation. On the target the FFT estimation runs in a separate
 * task; on the host it runs inline. */
void iq_balance_freq_process(iq_balance_freq_t *b, int16_t *samples, size_t complex_count);
/* Exposed for tests and diagnostics: run one estimation pass now. */
void iq_balance_freq_estimate(iq_balance_freq_t *b);
#ifdef ESP_PLATFORM
/* Create the background estimation task (target only). Returns false on OOM. */
bool iq_balance_freq_start_task(iq_balance_freq_t *b, int priority);
#endif
