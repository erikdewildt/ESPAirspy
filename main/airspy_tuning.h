#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool airspy_parse_calibration(const uint8_t *data,size_t size,int32_t *ppb);
double airspy_adjusted_frequency(uint32_t requested_hz,int32_t ppb);
bool airspy_parse_frequency_delta(const uint8_t *data,size_t size,double *hz);
