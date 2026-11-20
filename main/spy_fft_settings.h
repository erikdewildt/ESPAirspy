#pragma once
#include <stdint.h>

typedef struct {
    uint32_t center_hz, stage, pixels;
    int32_t db_offset;
    uint32_t db_range;
} spy_fft_settings_t;
typedef enum { SPY_FFT_SETTING_UNKNOWN, SPY_FFT_SETTING_OK, SPY_FFT_SETTING_INVALID } spy_fft_setting_result_t;
void spy_fft_settings_init(spy_fft_settings_t *settings, uint32_t center_hz);
/* Settings 200..205, with an unsigned wire representation for signed dB offset. */
spy_fft_setting_result_t spy_fft_settings_apply(spy_fft_settings_t *settings,uint32_t setting,uint32_t value,unsigned max_stage);
