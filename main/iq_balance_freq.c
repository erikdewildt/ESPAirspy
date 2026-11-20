/* Frequency-domain adaptive IQ-imbalance (image-rejection) correction.
 *
 * Derived from the BSD-3-Clause-style libairspyhf iqbalancer.c:
 *   Copyright (c) 2016-2023, Youssef Touil <youssef@airspy.com>
 *   Copyright (c) 2018, Leif Asbrink <leif@sm5bsz.com>
 * Adapted for the ESP32-P4: FFTBins reduced to 512, estimation throttled to a
 * few passes per second (the imbalance is quasi-static), and all working
 * buffers owned by the caller instead of heap-allocated.
 */
#include "iq_balance_freq.h"
#include "airspy_signal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define PI 3.14159265358979f
#define EPSILON 0.01f

/* ---- constants (upstream iqbalancer.h, scaled for FFTBins = 512) ---- */
#define BOOST_FACTOR 100000.0f
#define BINS_TO_OPTIMIZE (IQ_BAL_FREQ_FFT_BINS / 25)   /* 20 */
#define EDGE_BINS_TO_SKIP (IQ_BAL_FREQ_FFT_BINS / 22)  /* 23 */
#define CENTER_BINS_TO_SKIP 2
#define MAX_LOOKBACK 4
#define PHASE_STEP 1e-2f
#define AMPLITUDE_STEP 1e-2f
#define MAX_MU 50.0f
#define MIN_DELTA_MU 0.1f
#define DC_TIME_CONST 1e-4f
#define MINIMUM_POWER 0.01f
#define POWER_THRESHOLD 0.5f
#define BUFFERS_TO_SKIP_ON_RESET 2
#define MAX_POWER_DECAY 0.98f
#define MAX_POWER_RATIO 0.8f
#define BOOST_WINDOW_NORM (MAX_POWER_RATIO / 95.0f)

/* Embedded CPU adaptation: fewer FFTs per estimate and a lower estimate rate. */
#define FFT_INTEGRATION 2
#define FFT_OVERLAP 1
#define CORRELATION_INTEGRATION 2
#define WORKING_LEN (IQ_BAL_FREQ_FFT_BINS * (1 + FFT_INTEGRATION / FFT_OVERLAP)) /* 1536 */

/* Working-buffer fills between estimation passes (~500 fills/s at 768 kS/s,
 * so this yields ~16 estimates/s and keeps the added core load to a few %). */
#define EST_INTERVAL 32u

/* Upstream starting correction values for the HF+ zero-IF front end. */
#define INITIAL_PHASE 0.00006f
#define INITIAL_AMPLITUDE (-0.0045f)

/* int16 full-scale, to convert raw counts to the upstream [-1,1) float domain. */
#define I16_SCALE (1.0f / 32768.0f)

static float fft_window[IQ_BAL_FREQ_FFT_BINS];
static float boost_window[IQ_BAL_FREQ_FFT_BINS];
static int library_initialized = 0;

static void init_library(void)
{
    if (library_initialized) return;
    const int length = (int)IQ_BAL_FREQ_FFT_BINS - 1;
    for (int i = 0; i <= length; i++) {
        fft_window[i] = (float)(
            +0.35875
            - 0.48829 * cos(2.0 * PI * i / length)
            + 0.14128 * cos(4.0 * PI * i / length)
            - 0.01168 * cos(6.0 * PI * i / length));
        float x = i * 2.0f / (float)BINS_TO_OPTIMIZE;
        boost_window[i] = (float)(1.0 / BOOST_FACTOR + 1.0 / exp((double)(x * x)));
    }
    library_initialized = 1;
}

static void window(iq_balance_freq_complex_t *buffer, int length)
{
    for (int i = 0; i < length; i++) {
        buffer[i].re *= fft_window[i];
        buffer[i].im *= fft_window[i];
    }
}

/* In-place radix-2 DIT FFT with a final half-swap (FFT-shifted output). */
static void fft(iq_balance_freq_complex_t *buffer, int length)
{
    int nm1 = length - 1, nd2 = length / 2;
    int i, j, jm1, k, l, m, le, le2, ip;
    iq_balance_freq_complex_t u, t, r;

    m = 0; i = length;
    while (i > 1) { ++m; i >>= 1; }

    j = nd2;
    for (i = 1; i < nm1; ++i) {
        if (i < j) { t = buffer[j]; buffer[j] = buffer[i]; buffer[i] = t; }
        k = nd2;
        while (k <= j) { j -= k; k >>= 1; }
        j += k;
    }

    for (l = 1; l <= m; ++l) {
        le = 1 << l; le2 = le / 2;
        u.re = 1.0f; u.im = 0.0f;
        r.re = cosf(PI / le2); r.im = -sinf(PI / le2);
        for (j = 1; j <= le2; ++j) {
            jm1 = j - 1;
            for (i = jm1; i <= nm1; i += le) {
                ip = i + le2;
                t.re = u.re * buffer[ip].re - u.im * buffer[ip].im;
                t.im = u.im * buffer[ip].re + u.re * buffer[ip].im;
                buffer[ip].re = buffer[i].re - t.re;
                buffer[ip].im = buffer[i].im - t.im;
                buffer[i].re += t.re;
                buffer[i].im += t.im;
            }
            t.re = u.re * r.re - u.im * r.im;
            t.im = u.im * r.re + u.re * r.im;
            u.re = t.re; u.im = t.im;
        }
    }

    for (i = 0; i < nd2; i++) {
        j = nd2 + i;
        t = buffer[i]; buffer[i] = buffer[j]; buffer[j] = t;
    }
}

static iq_balance_freq_complex_t mul(iq_balance_freq_complex_t a, const iq_balance_freq_complex_t *b)
{
    iq_balance_freq_complex_t r;
    r.re = a.re * b->re - a.im * b->im;
    r.im = a.im * b->re + a.re * b->im;
    return r;
}

/* Apply a perturbed (phase, amplitude) correction in place; when
 * skip_power_calculation is false, return the pre-correction power sum. */
static float adjust_benchmark(float phase, float amplitude, iq_balance_freq_complex_t *iq, int skip_power_calculation)
{
    float sum = 0.0f;
    for (unsigned i = 0; i < IQ_BAL_FREQ_FFT_BINS; i++) {
        float re = iq[i].re, im = iq[i].im;
        iq[i].re += phase * im;
        iq[i].im += phase * re;
        iq[i].re *= 1.0f + amplitude;
        iq[i].im *= 1.0f - amplitude;
        if (!skip_power_calculation) sum += re * re + im * im;
    }
    return sum;
}

/* Correlate each FFT bin with its image to accumulate the imbalance estimate. */
static int compute_corr(iq_balance_freq_t *b, const iq_balance_freq_complex_t *iq,
                        iq_balance_freq_complex_t *ccorr, int length, int step)
{
    int count = 0;
    float phase = b->phase + step * PHASE_STEP;
    float amplitude = b->amplitude + step * AMPLITUDE_STEP;

    for (int n = 0, m = 0; n <= length - (int)IQ_BAL_FREQ_FFT_BINS && m < FFT_INTEGRATION;
         n += (int)IQ_BAL_FREQ_FFT_BINS / FFT_OVERLAP, m++) {
        memcpy(b->scratch, iq + n, IQ_BAL_FREQ_FFT_BINS * sizeof(iq_balance_freq_complex_t));
        float power = adjust_benchmark(phase, amplitude, b->scratch, step);
        if (step == 0) {
            if (power > MINIMUM_POWER) {
                b->power_flag[m] = 1;
                b->integrated_total_power += power;
            } else {
                b->power_flag[m] = 0;
            }
        }
        if (b->power_flag[m] == 1) {
            count++;
            window(b->scratch, (int)IQ_BAL_FREQ_FFT_BINS);
            fft(b->scratch, (int)IQ_BAL_FREQ_FFT_BINS);
            for (unsigned i = EDGE_BINS_TO_SKIP, j = IQ_BAL_FREQ_FFT_BINS - EDGE_BINS_TO_SKIP;
                 i <= IQ_BAL_FREQ_FFT_BINS - EDGE_BINS_TO_SKIP; i++, j--) {
                iq_balance_freq_complex_t cc = mul(b->scratch[i], &b->scratch[j]);
                ccorr[i].re += cc.re; ccorr[i].im += cc.im;
                ccorr[j].re = ccorr[i].re; ccorr[j].im = ccorr[i].im;
            }
            if (step == 0) {
                for (unsigned i = EDGE_BINS_TO_SKIP; i <= IQ_BAL_FREQ_FFT_BINS - EDGE_BINS_TO_SKIP; i++) {
                    float p = b->scratch[i].re * b->scratch[i].re + b->scratch[i].im * b->scratch[i].im;
                    b->boost[i] += p;
                    if (b->optimal_bin == (int)IQ_BAL_FREQ_FFT_BINS / 2) {
                        b->integrated_image_power += p;
                    } else {
                        int idx = abs((int)IQ_BAL_FREQ_FFT_BINS - (int)i - b->optimal_bin);
                        b->integrated_image_power += p * boost_window[idx];
                    }
                }
            }
        }
    }
    return count;
}

static iq_balance_freq_complex_t utility(iq_balance_freq_t *b, const iq_balance_freq_complex_t *ccorr)
{
    iq_balance_freq_complex_t acc = { 0, 0 };
    float invskip = 1.0f / (float)EDGE_BINS_TO_SKIP;
    for (unsigned i = EDGE_BINS_TO_SKIP, j = IQ_BAL_FREQ_FFT_BINS - EDGE_BINS_TO_SKIP;
         i <= IQ_BAL_FREQ_FFT_BINS - EDGE_BINS_TO_SKIP; i++, j--) {
        int distance = abs((int)i - (int)IQ_BAL_FREQ_FFT_BINS / 2);
        if (distance > CENTER_BINS_TO_SKIP) {
            float weight = (distance > (int)EDGE_BINS_TO_SKIP) ? 1.0f : (distance * invskip);
            if (b->optimal_bin != (int)IQ_BAL_FREQ_FFT_BINS / 2)
                weight *= boost_window[abs(b->optimal_bin - (int)i)];
            weight *= b->boost[j] / (b->boost[i] + EPSILON);
            acc.re += ccorr[i].re * weight;
            acc.im += ccorr[i].im * weight;
        }
    }
    return acc;
}

static void estimate_imbalance(iq_balance_freq_t *b, const iq_balance_freq_complex_t *iq, int length)
{
    float amplitude, phase, mu;
    iq_balance_freq_complex_t a, c;

    if (b->reset_flag) {
        b->reset_flag = 0;
        b->no_of_avg = -BUFFERS_TO_SKIP_ON_RESET;
        b->maximum_image_power = 0.0f;
    }
    if (b->no_of_avg < 0) { b->no_of_avg++; return; }
    else if (b->no_of_avg == 0) {
        b->integrated_image_power = 0.0f;
        b->integrated_total_power = 0.0f;
        memset(b->boost, 0, IQ_BAL_FREQ_FFT_BINS * sizeof(float));
        memset(b->corr, 0, IQ_BAL_FREQ_FFT_BINS * sizeof(iq_balance_freq_complex_t));
        memset(b->corr_plus, 0, IQ_BAL_FREQ_FFT_BINS * sizeof(iq_balance_freq_complex_t));
    }

    b->maximum_image_power *= MAX_POWER_DECAY;

    int i = compute_corr(b, iq, b->corr, length, 0);
    if (i == 0) return;

    b->no_of_avg += i;
    compute_corr(b, iq, b->corr_plus, length, 1);

    if (b->no_of_avg <= CORRELATION_INTEGRATION * FFT_INTEGRATION) return;
    b->no_of_avg = 0;

    if (b->optimal_bin == (int)IQ_BAL_FREQ_FFT_BINS / 2) {
        if (b->integrated_total_power < b->maximum_image_power) return;
        b->maximum_image_power = b->integrated_total_power;
    } else {
        if (b->integrated_image_power - b->integrated_total_power * BOOST_WINDOW_NORM <
            b->maximum_image_power * POWER_THRESHOLD) return;
        b->maximum_image_power = b->integrated_image_power - b->integrated_total_power * BOOST_WINDOW_NORM;
    }

    a = utility(b, b->corr);
    c = utility(b, b->corr_plus);

    mu = a.im - c.im;
    if (fabsf(mu) > MIN_DELTA_MU) {
        mu = a.im / mu;
        if (mu < -MAX_MU) mu = -MAX_MU;
        else if (mu > MAX_MU) mu = MAX_MU;
    } else {
        mu = 0.0f;
    }
    phase = b->phase + PHASE_STEP * mu;

    mu = a.re - c.re;
    if (fabsf(mu) > MIN_DELTA_MU) {
        mu = a.re / mu;
        if (mu < -MAX_MU) mu = -MAX_MU;
        else if (mu > MAX_MU) mu = MAX_MU;
    } else {
        mu = 0.0f;
    }
    amplitude = b->amplitude + AMPLITUDE_STEP * mu;

    if (b->no_of_raw < MAX_LOOKBACK) b->no_of_raw++;
    b->raw_amplitudes[b->raw_ptr] = amplitude;
    b->raw_phases[b->raw_ptr] = phase;
    int idx = b->raw_ptr;
    for (int j = 0; j < b->no_of_raw - 1; j++) {
        idx = (idx + MAX_LOOKBACK - 1) & (MAX_LOOKBACK - 1);
        phase += b->raw_phases[idx];
        amplitude += b->raw_amplitudes[idx];
    }
    phase /= b->no_of_raw;
    amplitude /= b->no_of_raw;
    b->raw_ptr = (b->raw_ptr + 1) & (MAX_LOOKBACK - 1);

    b->phase = phase;
    b->amplitude = amplitude;
}

void iq_balance_freq_init(iq_balance_freq_t *b)
{
    init_library();
    memset(b, 0, sizeof(*b));
    b->phase = INITIAL_PHASE;
    b->last_phase = INITIAL_PHASE;
    b->amplitude = INITIAL_AMPLITUDE;
    b->last_amplitude = INITIAL_AMPLITUDE;
    b->optimal_bin = (int)IQ_BAL_FREQ_FFT_BINS / 2;
    b->reset_flag = 1;
}

void iq_balance_freq_reset(iq_balance_freq_t *b)
{
    b->working_buffer_pos = 0;
    b->fills_since_estimate = 0;
    b->iavg = b->qavg = 0.0f;
    b->last_phase = INITIAL_PHASE;
    b->last_amplitude = INITIAL_AMPLITUDE;
    atomic_store(&b->reset_pending, true);
}

void iq_balance_freq_estimate(iq_balance_freq_t *b)
{
    estimate_imbalance(b, b->working_buffer, (int)WORKING_LEN);
}

void iq_balance_freq_process(iq_balance_freq_t *b, int16_t *samples, size_t complex_count)
{
    float inv = complex_count > 1 ? 1.0f / (float)(complex_count - 1) : 1.0f;
    float last_phase = b->last_phase, phase = b->phase;
    float last_amp = b->last_amplitude, amp = b->amplitude;

    for (size_t i = 0; i < complex_count; i++) {
        float im = (float)samples[2 * i] * I16_SCALE;      /* Q */
        float re = (float)samples[2 * i + 1] * I16_SCALE;  /* I */

        /* DC cancel (slow running average). */
        b->iavg += DC_TIME_CONST * (re - b->iavg);
        b->qavg += DC_TIME_CONST * (im - b->qavg);
        float re_dc = re - b->iavg;
        float im_dc = im - b->qavg;

        /* Accumulate the DC-cancelled (still uncorrected) sample and run the
         * estimation when the working buffer wraps. */
        b->working_buffer[b->working_buffer_pos].re = re_dc;
        b->working_buffer[b->working_buffer_pos].im = im_dc;
        if (++b->working_buffer_pos >= (int)WORKING_LEN) {
            b->working_buffer_pos = 0;
            if (++b->fills_since_estimate >= (int)EST_INTERVAL) {
                b->fills_since_estimate = 0;
#ifdef ESP_PLATFORM
                if (b->task) {
                    /* Hand the accumulated block to the estimation task; if it
                     * is still busy with the previous block, skip this one (the
                     * imbalance is quasi-static, so a dropped estimate is fine).
                     * This keeps the FFT work out of the USB callback. */
                    if (!atomic_load(&b->handoff_busy)) {
                        memcpy(b->ready_buffer, b->working_buffer, sizeof(b->working_buffer));
                        atomic_store(&b->handoff_busy, true);
                        xTaskNotifyGive((TaskHandle_t)b->task);
                    }
                } else
#endif
                {
                    estimate_imbalance(b, b->working_buffer, (int)WORKING_LEN);
                }
            }
        }

        /* Interpolated phase/amplitude correction. */
        float p = ((float)i * last_phase + (float)(complex_count - 1 - i) * phase) * inv;
        float a = ((float)i * last_amp + (float)(complex_count - 1 - i) * amp) * inv;
        float rc = re_dc + p * im_dc;
        float ic = im_dc + p * re_dc;
        re = rc * (1.0f + a);
        im = ic * (1.0f - a);

        samples[2 * i] = airspy_quantize_i16(im * 32768.0f);      /* Q */
        samples[2 * i + 1] = airspy_quantize_i16(re * 32768.0f);  /* I */
    }

    b->last_phase = phase;
    b->last_amplitude = amp;
}

#ifdef ESP_PLATFORM
/* Background estimation task: runs the FFT-based imbalance estimate off the
 * USB callback so that path stays predictable. The producer hands over a full
 * block through ready_buffer/handoff_busy and wakes this task. */
static void iq_balance_freq_task(void *arg)
{
    iq_balance_freq_t *b = (iq_balance_freq_t *)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (atomic_exchange(&b->reset_pending, false)) {
            b->phase = INITIAL_PHASE;
            b->amplitude = INITIAL_AMPLITUDE;
            b->no_of_avg = 0;
            b->no_of_raw = 0;
            b->raw_ptr = 0;
            b->maximum_image_power = 0.0f;
            b->integrated_total_power = 0.0f;
            b->integrated_image_power = 0.0f;
            b->reset_flag = 1;
            memset(b->corr, 0, sizeof(b->corr));
            memset(b->corr_plus, 0, sizeof(b->corr_plus));
            memset(b->boost, 0, sizeof(b->boost));
        }
        estimate_imbalance(b, b->ready_buffer, (int)WORKING_LEN);
        atomic_store(&b->handoff_busy, false);
    }
}

bool iq_balance_freq_start_task(iq_balance_freq_t *b, int priority)
{
    if (b->task) return true;
    TaskHandle_t h = NULL;
    if (xTaskCreatePinnedToCore(iq_balance_freq_task, "iqbal_freq", 4096, b, priority, &h, 1) != pdPASS)
        return false;
    b->task = h;
    return true;
}
#endif
