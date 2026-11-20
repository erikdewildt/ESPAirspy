#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "spy_fft.h"
typedef struct { uint32_t center_hz,sequence; uint8_t bins[SPY_FFT_SIZE];
#if CONFIG_AIRSPY_FFT_CAPTURE_TEST
    int16_t test_iq[SPY_FFT_SIZE*2];
#endif
} spy_fft_frame_t;
typedef struct { uint32_t submitted,completed,skipped; } spy_fft_live_stats_t;
esp_err_t spy_fft_live_start(void);
/* Single producer: the USB client task. Nonblocking when the pool is full. */
void spy_fft_live_submit(const int16_t *iq,size_t samples,uint32_t center_hz);
bool spy_fft_live_take(spy_fft_frame_t *frame);
void spy_fft_live_stats(spy_fft_live_stats_t *stats);
