#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "spy_fft.h"

typedef struct {
    uint32_t center_hz, sample_rate, stage, sequence, epoch;
    float bins_db[SPY_FFT_SIZE];
} spy_spectrum_live_frame_t;
typedef struct {
    uint32_t submitted, processed, dropped, resets, frames, max_process_us;
} spy_spectrum_live_stats_t;

/* Start once, before enabling the producer. Samples and frames are copied. */
esp_err_t spy_spectrum_live_start(unsigned stage);
/* Changing the stage invalidates queued output and resets the filter. */
bool spy_spectrum_live_set_stage(unsigned stage);
void spy_spectrum_live_set_enabled(bool enabled);
/* Single producer, nonblocking; loss is detected and resets the filter. */
void spy_spectrum_live_submit(const int16_t *iq, size_t samples, uint32_t center_hz, uint32_t sample_rate);
/* Signal a gap when the receiver is stopped or restarted. */
void spy_spectrum_live_discontinuity(void);
bool spy_spectrum_live_take(spy_spectrum_live_frame_t *frame);
void spy_spectrum_live_stats(spy_spectrum_live_stats_t *stats);
