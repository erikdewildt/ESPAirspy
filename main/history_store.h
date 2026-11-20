#pragma once
#include "metrics.h"
#define HISTORY_BATCH_SAMPLES 12
esp_err_t history_store_open(void (*restore)(const metric_sample_t *,size_t));
esp_err_t history_store_write(const metric_sample_t *samples,size_t count);
