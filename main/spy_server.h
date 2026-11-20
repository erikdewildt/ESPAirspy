#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
uint64_t spy_server_bytes_sent(void);
uint32_t spy_server_connections(void);
bool spy_server_client_connected(void);
bool spy_server_listening(void);
esp_err_t spy_server_start(void);

uint32_t spy_server_iq_sample_rate(void);
uint32_t spy_server_iq_frequency(void);
uint64_t spy_server_clipped_components(void);

typedef struct {
    uint64_t samples_sent;
    uint32_t empty_reads;
    uint32_t send_failures;
    uint32_t max_send_us;
} spy_iq_stats_t;

void spy_server_iq_stats(spy_iq_stats_t *stats);
