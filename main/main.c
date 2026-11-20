#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_eth.h"
#include "nvs_flash.h"
#include "ethernet_init.h"
#include "usb/usb_host.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/semphr.h"
#include "web.h"
#include "airspy.h"
#include "settings.h"
#include "metrics.h"
#include "spy_server.h"
#if CONFIG_AIRSPY_SPECTRUM_CAPTURE_TEST || CONFIG_AIRSPY_FFT_SERVER
#include "spy_spectrum_live.h"
#endif
#if CONFIG_AIRSPY_FFT_CAPTURE_TEST
#include "spy_fft_live.h"
#include "spy_fft_capture_test.h"
#endif
#if CONFIG_AIRSPY_FFT_BENCHMARK
#include "spy_fft_bench.h"
#endif

static const char *TAG = "airspy";
static SemaphoreHandle_t usb_ready;
static void save_history_before_restart(void)
{
    esp_err_t err=metrics_flush();
    if(err!=ESP_OK)ESP_LOGW(TAG,"Could not save pending history: %s",esp_err_to_name(err));
}
static void got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *event = data;
    esp_err_t e=esp_netif_sntp_start();
    if(e!=ESP_OK)ESP_LOGW(TAG,"Clock sync start: %s",esp_err_to_name(e));
    ESP_LOGI(TAG, "Ethernet IP: " IPSTR, IP2STR(&event->ip_info.ip));
}
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    settings_t appcfg; settings_load(&appcfg);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip, NULL));
    esp_eth_handle_t *handles = NULL;
    uint8_t count = 0;
    ESP_ERROR_CHECK(example_eth_init(&handles, &count));
    if (count != 1) { ESP_LOGE(TAG, "Expected one Ethernet interface"); return; }
    esp_netif_config_t netcfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&netcfg);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_eth_new_netif_glue(handles[0])));
    if (appcfg.net_mode == SETTINGS_NET_STATIC) {
        esp_err_t de = esp_netif_dhcpc_stop(netif);
        if (de != ESP_OK && de != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
            ESP_LOGW(TAG, "DHCP stop: %s", esp_err_to_name(de));
        esp_netif_ip_info_t ip = {0};
        ip.ip.addr = appcfg.ip; ip.netmask.addr = appcfg.netmask; ip.gw.addr = appcfg.gateway;
        esp_err_t se = esp_netif_set_ip_info(netif, &ip);
        if (se != ESP_OK) ESP_LOGE(TAG, "Static IP: %s", esp_err_to_name(se));
        if (appcfg.dns) {
            esp_netif_dns_info_t d = {0};
            d.ip.type = ESP_IPADDR_TYPE_V4; d.ip.u_addr.ip4.addr = appcfg.dns;
            esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &d);
        }
        ESP_LOGI(TAG, "Network: static IP configured");
    } else {
        ESP_LOGI(TAG, "Network: DHCP");
    }
    esp_sntp_config_t clock=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    clock.start=false;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&clock));
    ESP_ERROR_CHECK(esp_eth_start(handles[0]));
    ESP_ERROR_CHECK(metrics_start());
    ESP_ERROR_CHECK(esp_register_shutdown_handler(save_history_before_restart));
    ESP_ERROR_CHECK(web_start(handles[0]));
    usb_ready = xSemaphoreCreateBinary();
    if (!usb_ready || airspy_start(usb_ready) != ESP_OK) esp_restart();
    if (xSemaphoreTake(usb_ready, pdMS_TO_TICKS(10000)) != pdTRUE) { ESP_LOGE(TAG, "USB startup failed"); esp_restart(); }
    airspy_set_dsp_options(appcfg.dc_block != 0, appcfg.iq_balance != 0);
    airspy_request_gain(appcfg.gain);
    airspy_request_lna(appcfg.lna);
    airspy_request_agc_threshold(appcfg.agc_threshold);
#if CONFIG_AIRSPY_SPECTRUM_CAPTURE_TEST
    ESP_ERROR_CHECK(spy_spectrum_live_start(CONFIG_AIRSPY_SPECTRUM_TEST_STAGE));
#elif CONFIG_AIRSPY_FFT_SERVER
    ESP_ERROR_CHECK(spy_spectrum_live_start(0));
    spy_spectrum_live_set_enabled(false);
#endif
    ESP_ERROR_CHECK(spy_server_start());
#if CONFIG_AIRSPY_FFT_CAPTURE_TEST
    ESP_ERROR_CHECK(spy_fft_live_start());
    ESP_ERROR_CHECK(spy_fft_capture_test_start());
#endif
#if CONFIG_AIRSPY_FFT_BENCHMARK
    ESP_ERROR_CHECK(spy_fft_benchmark_start());
#endif
    // Give the receiver driver time to initialize before confirming a new image.
    vTaskDelay(pdMS_TO_TICKS(10000));
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY)
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
}
