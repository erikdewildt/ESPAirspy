#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Persistent application settings in the default NVS partition.
 * IP fields are stored in the raw esp_ip4_addr_t.addr representation
 * (i.e. what esp_netif_str_to_ip4 / esp_ip4addr_ntoa use), 0 = unset. */

#define SETTINGS_NET_DHCP   0
#define SETTINGS_NET_STATIC 1

typedef struct {
    uint8_t  net_mode;   /* SETTINGS_NET_DHCP / SETTINGS_NET_STATIC */
    uint32_t ip;         /* static IPv4 */
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;        /* 0 = none */
    uint8_t  dc_block;   /* runtime DSP toggle */
    uint8_t  iq_balance; /* runtime DSP toggle */
    uint8_t  gain;       /* receiver gain index 0..9 (0 = AGC) */
    uint8_t  lna;        /* 0 = off, 1 = +6 dB postamp */
    uint8_t  agc_threshold; /* 0 = low, 1 = high */
} settings_t;

/* Fill defaults (DHCP; DSP toggles follow the build configuration). */
void settings_defaults(settings_t *s);
/* Load from NVS, falling back to defaults for any missing/invalid key.
 * Returns ESP_OK with defaults when NVS is unavailable. */
esp_err_t settings_load(settings_t *s);
/* Persist all fields to NVS and return the first write or commit error.
 * NVS may retain earlier writes if a later field fails. */
esp_err_t settings_save(const settings_t *s);
