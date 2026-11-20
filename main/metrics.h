#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint32_t uptime; uint16_t cpu10, ram10, psram10, flash10; uint32_t net_kbps; } metric_sample_t;
typedef struct { uint32_t ram_total, ram_free, flash_total, flash_reserved, app_size; uint16_t core_cpu10[2]; bool history_ready; uint32_t history_saved,history_errors; metric_sample_t latest; } metrics_snapshot_t;
esp_err_t metrics_start(void);
void metrics_snapshot(metrics_snapshot_t *out);
size_t metrics_history(metric_sample_t *out, size_t capacity, unsigned seconds);

uint32_t metrics_now(void);
esp_err_t metrics_flush(void);
