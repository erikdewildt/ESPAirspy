#include "settings.h"
#include <string.h>
#include "nvs.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "settings";
static const char *NS = "appcfg";

static uint8_t default_dc(void)
{
#if CONFIG_AIRSPY_DC_REMOVE
    return 1;
#else
    return 0;
#endif
}

static uint8_t default_iq(void)
{
#if CONFIG_AIRSPY_IQ_BALANCE || CONFIG_AIRSPY_IQ_BALANCE_FREQ
    return 1;
#else
    return 0;
#endif
}

void settings_defaults(settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->net_mode = SETTINGS_NET_DHCP;
    s->dc_block = default_dc();
    s->iq_balance = default_iq();
    s->gain = 0;            /* AGC */
    s->lna = 0;            /* off */
    s->agc_threshold = 0;  /* low */
}

esp_err_t settings_load(settings_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    settings_defaults(s);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return ESP_OK; /* not initialised yet */
    uint8_t u8;
    uint32_t u32;
    if (nvs_get_u8(h, "net_mode", &u8) == ESP_OK) s->net_mode = u8;
    if (nvs_get_u32(h, "ip", &u32) == ESP_OK) s->ip = u32;
    if (nvs_get_u32(h, "mask", &u32) == ESP_OK) s->netmask = u32;
    if (nvs_get_u32(h, "gw", &u32) == ESP_OK) s->gateway = u32;
    if (nvs_get_u32(h, "dns", &u32) == ESP_OK) s->dns = u32;
    if (nvs_get_u8(h, "dc", &u8) == ESP_OK) s->dc_block = u8;
    if (nvs_get_u8(h, "iq", &u8) == ESP_OK) s->iq_balance = u8;
    if (nvs_get_u8(h, "gain", &u8) == ESP_OK) s->gain = u8;
    if (nvs_get_u8(h, "lna", &u8) == ESP_OK) s->lna = u8;
    if (nvs_get_u8(h, "thr", &u8) == ESP_OK) s->agc_threshold = u8;
    nvs_close(h);
    if (s->net_mode != SETTINGS_NET_DHCP && s->net_mode != SETTINGS_NET_STATIC)
        s->net_mode = SETTINGS_NET_DHCP;
    if (s->gain > 9) s->gain = 0;
    if (s->lna > 1) s->lna = 0;
    if (s->agc_threshold > 1) s->agc_threshold = 0;
    return ESP_OK;
}

esp_err_t settings_save(const settings_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) { ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(e)); return e; }
    e = nvs_set_u8(h, "net_mode", s->net_mode);
    if (e == ESP_OK) e = nvs_set_u32(h, "ip", s->ip);
    if (e == ESP_OK) e = nvs_set_u32(h, "mask", s->netmask);
    if (e == ESP_OK) e = nvs_set_u32(h, "gw", s->gateway);
    if (e == ESP_OK) e = nvs_set_u32(h, "dns", s->dns);
    if (e == ESP_OK) e = nvs_set_u8(h, "dc", s->dc_block);
    if (e == ESP_OK) e = nvs_set_u8(h, "iq", s->iq_balance);
    if (e == ESP_OK) e = nvs_set_u8(h, "gain", s->gain);
    if (e == ESP_OK) e = nvs_set_u8(h, "lna", s->lna);
    if (e == ESP_OK) e = nvs_set_u8(h, "thr", s->agc_threshold);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
