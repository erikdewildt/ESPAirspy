#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define SPY_FFT_SIZE 2048u
/* Caller owns this workspace. No heap allocation occurs in compute. */
typedef struct {
    float real[SPY_FFT_SIZE],imag[SPY_FFT_SIZE],window[SPY_FFT_SIZE];
    float twiddle_real[SPY_FFT_SIZE/2],twiddle_imag[SPY_FFT_SIZE/2];
    float coherent_gain;
} spy_fft_t;
void spy_fft_init(spy_fft_t *fft);
/* Output is FFT-shifted: negative frequency first, DC halfway through.
 * Bytes follow SpyServer's offset - range*(1-byte/255) convention. */
void spy_fft_compute(spy_fft_t *fft,const int16_t *iq,uint8_t *bins,float db_offset,float db_range);
/* Float input is in the same full-scale units as the 16-bit input, and may
 * contain fractional counts preserved by the decimation filters. */
void spy_fft_compute_f32(spy_fft_t *fft,const float *iq,uint8_t *bins,float db_offset,float db_range);
/* Preserve spectrum levels for later mapping to a client's dB window. */
void spy_fft_compute_f32_db(spy_fft_t *fft,const float *iq,float *bins_db);
/* Remap an FFT-shifted spectrum onto pixel centers in a requested RF window.
 * The output buffer must not overlap the full input spectrum.
 * center_offset and span are in Hz relative to the input FFT's center. */
bool spy_fft_render(const uint8_t *full,uint8_t *out,size_t pixels,float input_rate,float center_offset,float span);
/* Render dB bins and apply the SpyServer window after resampling. */
bool spy_fft_render_db(const float *full_db,uint8_t *out,size_t pixels,float input_rate,float center_offset,float span,float db_offset,float db_range);
