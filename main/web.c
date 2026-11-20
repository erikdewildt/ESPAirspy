#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "metrics.h"
#include "airspy.h"
#include "airspy_update.h"
#include "airspy_update_check.h"
#include "mbedtls/sha256.h"
#include "spy_server.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif
#include "nvs.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "web.h"
#include "settings.h"
#include "esp_netif.h"
#if CONFIG_AIRSPY_SPECTRUM_CAPTURE_TEST || CONFIG_AIRSPY_FFT_SERVER
#include "spy_spectrum_live.h"
#endif
#if CONFIG_AIRSPY_FFT_CAPTURE_TEST
#include "spy_fft_live.h"
_Static_assert(sizeof(spy_fft_frame_t)==8+SPY_FFT_SIZE+SPY_FFT_SIZE*4,"Unexpected test frame layout");
#endif

static const char *TAG = "web";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool airspy_seen;
void web_note_airspy(void) { portENTER_CRITICAL(&state_lock); airspy_seen = true; portEXIT_CRITICAL(&state_lock); }

extern const char page_start[] asm("_binary_page_html_start");
static esp_err_t index_get(httpd_req_t *req) { httpd_resp_set_type(req,"text/html; charset=utf-8"); return httpd_resp_sendstr(req,page_start); }
#if CONFIG_AIRSPY_SPECTRUM_CAPTURE_TEST || CONFIG_AIRSPY_FFT_SERVER
static esp_err_t spectrum_stats_get(httpd_req_t *req)
{
    spy_spectrum_live_stats_t s;spy_spectrum_live_stats(&s);
    char json[256];snprintf(json,sizeof(json),"{\"submitted\":%" PRIu32 ",\"processed\":%" PRIu32 ",\"dropped\":%" PRIu32 ",\"resets\":%" PRIu32 ",\"frames\":%" PRIu32 ",\"max_process_us\":%" PRIu32 "}",s.submitted,s.processed,s.dropped,s.resets,s.frames,s.max_process_us);
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,json);
}
#endif
#if CONFIG_AIRSPY_SPECTRUM_CAPTURE_TEST
static esp_err_t spectrum_frame_get(httpd_req_t *req)
{
    spy_spectrum_live_frame_t *frame=malloc(sizeof(*frame));if(!frame)return ESP_ERR_NO_MEM;
    if(!spy_spectrum_live_take(frame)){free(frame);httpd_resp_set_status(req,"204 No Content");return httpd_resp_send(req,NULL,0);}
    httpd_resp_set_type(req,"application/octet-stream");esp_err_t e=httpd_resp_send(req,(const char *)frame,sizeof(*frame));free(frame);return e;
}
#endif
#if CONFIG_AIRSPY_FFT_CAPTURE_TEST
static esp_err_t fft_test_get(httpd_req_t *req)
{
    spy_fft_frame_t *frame=malloc(sizeof(*frame));if(!frame)return ESP_ERR_NO_MEM;
    if(!spy_fft_live_take(frame)) { free(frame);httpd_resp_set_status(req,"204 No Content");return httpd_resp_send(req,NULL,0); }
    httpd_resp_set_type(req,"application/octet-stream");esp_err_t e=httpd_resp_send(req,(const char *)frame,sizeof(*frame));free(frame);return e;
}
#endif
static esp_err_t status_get(httpd_req_t *req)
{
    char json[2048]; metrics_snapshot_t m; metrics_snapshot(&m);
    nvs_stats_t nvs={0}; nvs_get_stats(NULL,&nvs);
    uint64_t input_samples,input_near_rail,mixer_clipped;airspy_input_level_counts(&input_samples,&input_near_rail,&mixer_clipped);
    const esp_partition_t *p=esp_ota_get_running_partition();
    int json_len=snprintf(json,sizeof(json),"{\"version\":\"%s\",\"slot\":\"%s\",\"cpu_freq_mhz\":%d,\"uptime_s\":%" PRId64 ",\"free_heap\":%" PRIu32 ",\"ram_total\":%" PRIu32 ",\"flash_total\":%" PRIu32 ",\"flash_reserved\":%" PRIu32 ",\"app_size\":%" PRIu32 ",\"cpu10\":%u,\"cpu0_10\":%u,\"cpu1_10\":%u,\"ram10\":%u,\"flash10\":%u,\"airspy_connected\":%s,\"airspy_firmware\":\"%s\",\"sample_rate\":%" PRIu32 ",\"iq_sample_rate\":%" PRIu32 ",\"frequency\":%" PRIu32 ",\"iq_frequency\":%" PRIu32 ",\"lo_frequency\":%" PRIu32 ",\"receiver_gain\":%" PRIu32 ",\"receiver_lna\":%" PRIu32 ",\"receiver_agc_threshold\":%" PRIu32 ",\"calibration_ppb\":%" PRId32 ",\"calibration_valid\":%s,\"usb_completions\":%" PRIu32 ",\"usb_empty\":%" PRIu32 ",\"usb_blocks\":%" PRIu32 ",\"dropped_blocks\":%" PRIu32 ",\"input_iq_samples\":%" PRIu64 ",\"input_near_rail\":%" PRIu64 ",\"mixer_clipped_components\":%" PRIu64 ",\"digital_clipped_components\":%" PRIu64 ",\"spy_connections\":%" PRIu32 ",\"spy_bytes\":%" PRIu64 ",\"spy_client\":%s,\"network_kbps\":%" PRIu32 ",\"heap_min\":%" PRIu32 ",\"heap_largest\":%u,\"tasks\":%u,\"nvs_used\":%u,\"nvs_total\":%u,\"idf_version\":\"%s\",\"history_now\":%u,\"history_ready\":%s,\"history_saved\":%u,\"history_errors\":%u}", esp_app_get_description()->version,p?p->label:"unknown",(int)(esp_clk_cpu_freq()/1000000),esp_timer_get_time()/1000000,m.ram_free,m.ram_total,m.flash_total,m.flash_reserved,m.app_size,m.latest.cpu10,m.core_cpu10[0],m.core_cpu10[1],m.latest.ram10,m.latest.flash10,airspy_connected()?"true":"false",airspy_firmware_version(),airspy_sample_rate(),spy_server_iq_sample_rate(),airspy_frequency(),spy_server_iq_frequency(),airspy_lo_frequency(),airspy_gain(),airspy_lna(),airspy_agc_threshold(),airspy_calibration_ppb(),airspy_calibration_valid()?"true":"false",airspy_usb_completions(),airspy_usb_empty(),airspy_received_blocks(),airspy_dropped_blocks(),input_samples,input_near_rail,mixer_clipped,spy_server_clipped_components(),spy_server_connections(),spy_server_bytes_sent(),spy_server_client_connected()?"true":"false",m.latest.net_kbps,(uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)uxTaskGetNumberOfTasks(),(unsigned)nvs.used_entries,(unsigned)nvs.total_entries,esp_get_idf_version(),(unsigned)metrics_now(),m.history_ready?"true":"false",(unsigned)m.history_saved,(unsigned)m.history_errors);
    if (json_len < 1 || (size_t)json_len >= sizeof(json)) return ESP_ERR_INVALID_SIZE;
    size_t chip_bytes = 0;
    const char *psram_state = "disabled";
#if CONFIG_SPIRAM
    chip_bytes = esp_psram_get_size();
    psram_state = esp_psram_is_initialized() ? "active" : "unavailable";
#endif
    eth_speed_t speed;
    eth_duplex_t duplex;
    unsigned ethernet_mbps = 0;
    const char *ethernet_duplex = "unknown";
    if (esp_eth_ioctl(req->user_ctx, ETH_CMD_G_SPEED, &speed) == ESP_OK) {
        if (speed == ETH_SPEED_10M) ethernet_mbps = 10;
        if (speed == ETH_SPEED_100M) ethernet_mbps = 100;
    }
    if (esp_eth_ioctl(req->user_ctx, ETH_CMD_G_DUPLEX_MODE, &duplex) == ESP_OK)
        ethernet_duplex = duplex == ETH_DUPLEX_FULL ? "full" : "half";
    uint64_t ring_lost_samples;
    size_t ring_capacity,ring_peak_estimate;
    airspy_iq_buffer_stats(&ring_lost_samples,&ring_capacity,&ring_peak_estimate);
    spy_iq_stats_t iq;spy_server_iq_stats(&iq);
    // Append transport counters and capacities, replacing the closing brace.
    size_t pos = (size_t)json_len - 1;
    int added = snprintf(json + pos, sizeof(json) - pos,
        ",\"spy_listening\":%s,\"iq_streaming\":%s,\"ethernet_mbps\":%u,\"ethernet_duplex\":\"%s\""
        ",\"psram_state\":\"%s\",\"psram_chip_bytes\":%zu,\"psram_heap_total\":%zu,\"psram_heap_free\":%zu,"
        "\"internal_heap_total\":%zu,\"internal_heap_free\":%zu,"
        "\"iq_ring_lost_samples\":%" PRIu64 ",\"iq_ring_capacity_bytes\":%zu,\"iq_ring_peak_estimate_bytes\":%zu,"
        "\"iq_samples_sent\":%" PRIu64 ",\"iq_empty_reads\":%" PRIu32 ",\"iq_send_failures\":%" PRIu32 ",\"iq_max_send_us\":%" PRIu32 "}",
        spy_server_listening()?"true":"false", airspy_iq_streaming()?"true":"false",
        ethernet_mbps, ethernet_duplex, psram_state, chip_bytes,
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        ring_lost_samples,ring_capacity,ring_peak_estimate,
        iq.samples_sent,iq.empty_reads,iq.send_failures,iq.max_send_us);
    if (added < 0 || (size_t)added >= sizeof(json) - pos) return ESP_ERR_INVALID_SIZE;
    httpd_resp_set_type(req,"application/json"); httpd_resp_set_hdr(req,"Cache-Control","no-store"); return httpd_resp_sendstr(req,json);
}
static esp_err_t time_post(httpd_req_t *req)
{
    char header[8];
    if(httpd_req_get_hdr_value_str(req,"X-Device-Time",header,sizeof(header))!=ESP_OK || strcmp(header,"1"))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Missing time header");
    char body[24];
    if(req->content_len<1 || req->content_len>=(int)sizeof(body))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid time");
    int used=0;
    while(used<req->content_len) { int n=httpd_req_recv(req,body+used,req->content_len-used);if(n<=0)return ESP_FAIL;used+=n; }
    body[used]=0;char *end;unsigned long long stamp=strtoull(body,&end,10);
    if(*end || stamp<1700000000ULL || stamp>4102444800ULL)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid time");
    if(metrics_now()<1700000000u) { struct timeval tv={.tv_sec=(time_t)stamp};if(settimeofday(&tv,NULL)!=0)return ESP_FAIL; }
    return httpd_resp_sendstr(req,"OK");
}
static esp_err_t history_get(httpd_req_t *req)
{
    char query[32]={0}, range[8]={0}; unsigned seconds=300;
    if(httpd_req_get_url_query_str(req,query,sizeof(query))==ESP_OK && httpd_query_key_value(query,"range",range,sizeof(range))==ESP_OK) { if(!strcmp(range,"1h"))seconds=3600;else if(!strcmp(range,"1d"))seconds=86400;else if(strcmp(range,"5m"))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid range"); }
    size_t cap=seconds==86400?1440:720;
    metric_sample_t *samples=heap_caps_malloc_prefer(cap*sizeof(*samples), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); if(!samples)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    size_t count=metrics_history(samples,cap,seconds); httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t e=httpd_resp_send_chunk(req,"{\"samples\":[",12);
    char buf[1024]; size_t used=0;
    for(size_t i=0;e==ESP_OK && i<count;i++) { char row[140];int n=snprintf(row,sizeof(row),"%s{\"t\":%" PRIu32 ",\"cpu10\":%u,\"ram10\":%u,\"psram10\":%u,\"flash10\":%u,\"net_kbps\":%" PRIu32 "}",i?",":"",samples[i].uptime,samples[i].cpu10,samples[i].ram10,samples[i].psram10,samples[i].flash10,samples[i].net_kbps);if(used+(size_t)n>sizeof(buf)){e=httpd_resp_send_chunk(req,buf,used);used=0;}if(e==ESP_OK){memcpy(buf+used,row,n);used+=n;} }
    if(e==ESP_OK && used) e=httpd_resp_send_chunk(req,buf,used);
    if(e==ESP_OK) e=httpd_resp_send_chunk(req,"]}",2);
    if(e==ESP_OK) e=httpd_resp_send_chunk(req,NULL,0);
    free(samples);
    return e;
}
static void restart_task(void *arg) { vTaskDelay(pdMS_TO_TICKS(1000)); esp_restart(); }
static esp_err_t airspy_calibration_get(httpd_req_t *req)
{
    uint8_t data[256];if(!airspy_configuration_backup(data))return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Airspy configuration unavailable");
    const char *serial=airspy_serial_number();if(!*serial)return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Airspy serial unavailable");
    char disposition[96];snprintf(disposition,sizeof(disposition),"attachment; filename=\"airspy-%s-configuration.bin\"",serial);
    httpd_resp_set_type(req,"application/octet-stream");httpd_resp_set_hdr(req,"Content-Disposition",disposition);httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,(const char *)data,sizeof(data));
}
/* Read-only preflight. No receiver commands or flash writes occur here. */
static esp_err_t airspy_verify_post(httpd_req_t *req)
{
    char header[8];
    if(httpd_req_get_hdr_value_str(req,"X-Airspy-Verify",header,sizeof(header))!=ESP_OK || strcmp(header,"1"))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Missing verification header");
    if(!airspy_connected())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Airspy not connected");
    char filename[64];
    if(httpd_req_get_hdr_value_str(req,"X-Airspy-Filename",filename,sizeof(filename))!=ESP_OK)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Missing file name header");
    airspy_image_check_t result=airspy_update_check(filename,airspy_firmware_version());
    if(result==AIRSPY_IMAGE_WRONG_VARIANT)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Receiver variant is not BB or CD");
    if(result==AIRSPY_IMAGE_WRONG_IMAGE)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Wrong image for this receiver");
    if(result!=AIRSPY_IMAGE_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"File name does not identify an Airspy image");
    if(req->content_len<1 || req->content_len>AIRSPY_IMAGE_MAX_SIZE)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Unsupported firmware file size");
    /* Drain the body without storing it. */
    char buf[1024];int left=req->content_len;
    while(left>0) { int n=httpd_req_recv(req,buf,left<(int)sizeof(buf)?left:(int)sizeof(buf)); if(n<=0)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Upload interrupted"); left-=n; }
    char json[96];
    snprintf(json,sizeof(json),"{\"valid\":true,\"image\":\"%s\",\"written\":false}",airspy_expected_image_name(airspy_firmware_version()));
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,json);
}
/* Validate the file name, stage the image in flash, then queue it for
 * programming on the receiver. */
static esp_err_t airspy_update_post(httpd_req_t *req)
{
    char header[8];
    if(httpd_req_get_hdr_value_str(req,"X-Airspy-Update",header,sizeof(header))!=ESP_OK || strcmp(header,"1"))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Missing update header");
    if(!airspy_connected())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Airspy not connected");
    if(airspy_update_in_progress())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Update already in progress");
    char filename[64];
    if(httpd_req_get_hdr_value_str(req,"X-Airspy-Filename",filename,sizeof(filename))!=ESP_OK)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Missing file name header");
    airspy_image_check_t result=airspy_update_check(filename,airspy_firmware_version());
    if(result==AIRSPY_IMAGE_WRONG_VARIANT)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Receiver variant is not BB or CD");
    if(result==AIRSPY_IMAGE_WRONG_IMAGE)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Wrong image for this receiver");
    if(result!=AIRSPY_IMAGE_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"File name does not identify an Airspy image");
    if(req->content_len<1 || req->content_len>AIRSPY_IMAGE_MAX_SIZE)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Unsupported firmware file size");
    if(airspy_update_scratch_prepare()!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Scratch erase failed");
    int left=req->content_len;int off=0;
    char buf[1024];
    while(left>0) {
        int n=httpd_req_recv(req,buf,left<(int)sizeof(buf)?left:(int)sizeof(buf));
        if(n<=0)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Upload interrupted");
        if(airspy_update_scratch_write((size_t)off,buf,(size_t)n)!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Scratch write failed");
        left-=n;off+=n;
    }
    if(!airspy_update_submit((size_t)req->content_len))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Update already in progress");
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,"{\"started\":true}");
}
static esp_err_t airspy_update_status_get(httpd_req_t *req)
{
    airspy_update_status_t s;airspy_update_status(&s);
    const char *state="idle";
    switch(s.state) {
    case AIRSPY_UPDATE_IDLE: state="idle"; break;
    case AIRSPY_UPDATE_REBOOTING: state="rebooting"; break;
    case AIRSPY_UPDATE_WAIT_SAMBA: state="waiting_bootloader"; break;
    case AIRSPY_UPDATE_PROGRAMMING: state="programming"; break;
    case AIRSPY_UPDATE_BOOTING: state="booting"; break;
    case AIRSPY_UPDATE_RECOVERING: state="recovering"; break;
    case AIRSPY_UPDATE_DONE: state="done"; break;
    case AIRSPY_UPDATE_FAILED: state="failed"; break;
    }
    char json[160];
    snprintf(json,sizeof(json),"{\"state\":\"%s\",\"progress\":%u,\"message\":\"%s\"}",state,s.progress,s.message?s.message:"");
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,json);
}
/* Recovery: set the receiver's boot-from-flash bit and reset, without flashing.
 * Used when a receiver was left in SAM-BA mode. */
static esp_err_t airspy_recover_post(httpd_req_t *req)
{
    char header[8];
    if(httpd_req_get_hdr_value_str(req,"X-Airspy-Recover",header,sizeof(header))!=ESP_OK || strcmp(header,"1"))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Missing recover header");
    airspy_recover_request();
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,"{\"started\":true}");
}
static void full_restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500)); /* let the response flush */
    airspy_power_cycle_and_restart();
}
/* Full restart: briefly de-power the USB host port (resetting the receiver),
 * then reboot the ESP32. */
static esp_err_t restart_post(httpd_req_t *req)
{
    char header[8];
    if(httpd_req_get_hdr_value_str(req,"X-Restart",header,sizeof(header))!=ESP_OK || strcmp(header,"1"))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Missing restart header");
    httpd_resp_sendstr(req,"Restarting...");
    if(xTaskCreate(full_restart_task,"full_restart",4096,NULL,5,NULL)!=pdPASS) esp_restart();
    return ESP_OK;
}
/* Clear the persisted application settings (NVS "appcfg") and restart so the
 * build defaults apply again. The history partition is separate and is kept. */
static esp_err_t factory_reset_post(httpd_req_t *req)
{
    char header[8];
    if(httpd_req_get_hdr_value_str(req,"X-Factory-Reset",header,sizeof(header))!=ESP_OK || strcmp(header,"1"))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Missing reset header");
    nvs_handle_t h;
    if(nvs_open("appcfg",NVS_READWRITE,&h)==ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    esp_err_t e=httpd_resp_sendstr(req,"{\"reset\":true}");
    if(xTaskCreate(restart_task,"factory_reset",4096,NULL,5,NULL)!=pdPASS) esp_restart();
    return e;
}
static esp_err_t ota_post(httpd_req_t *req)
{
    char header[8];
    if (httpd_req_get_hdr_value_str(req,"X-OTA-Upload",header,sizeof(header)) != ESP_OK || strcmp(header,"1"))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Missing upload header");
    const esp_partition_t *part=esp_ota_get_next_update_partition(NULL);
    if (!part || req->content_len <= 0 || (size_t)req->content_len > part->size)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid image size");
    esp_ota_handle_t handle; esp_err_t e=esp_ota_begin(part,req->content_len,&handle);
    if(e != ESP_OK) { ESP_LOGE(TAG,"OTA begin: %s",esp_err_to_name(e)); return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"OTA start failed"); }
    char buf[2048]; int left=req->content_len;
    while(left>0) {
        int n=httpd_req_recv(req,buf,left<(int)sizeof(buf)?left:(int)sizeof(buf));
        if(n<=0) { esp_ota_abort(handle); return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Upload interrupted"); }
        e=esp_ota_write(handle,buf,n); if(e!=ESP_OK) { esp_ota_abort(handle); return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Flash write failed"); } left-=n;
    }
    e=esp_ota_end(handle);
    if(e!=ESP_OK) { ESP_LOGE(TAG,"OTA image validation: %s",esp_err_to_name(e)); return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid firmware image"); }
    e=esp_ota_set_boot_partition(part);
    if(e!=ESP_OK) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not select boot partition");
    e=httpd_resp_sendstr(req,"Firmware received. The device is restarting.");
    if(xTaskCreate(restart_task,"ota_restart",6144,NULL,5,NULL)!=pdPASS) { ESP_LOGE(TAG,"Restart task failed; restarting now"); esp_restart(); }
    return e;
}
/* Set receiver gain / LNA / AGC-threshold from URL query parameters.
 * Any omitted parameter is left unchanged. */
static esp_err_t airspy_gain_post(httpd_req_t *req)
{
    char query[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "gain", val, sizeof(val)) == ESP_OK) {
            unsigned long g = strtoul(val, NULL, 10);
            if (g <= AIRSPY_GAIN_MAX) airspy_request_gain((uint32_t)g);
        }
        if (httpd_query_key_value(query, "lna", val, sizeof(val)) == ESP_OK)
            airspy_request_lna(strtoul(val, NULL, 10) ? 1 : 0);
        if (httpd_query_key_value(query, "threshold", val, sizeof(val)) == ESP_OK)
            airspy_request_agc_threshold(strtoul(val, NULL, 10) ? 1 : 0);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
/* ---- Application settings: network + runtime DSP experiments ---- */
static void ip_to_str(uint32_t addr, char *out, size_t n)
{
    if (addr) { esp_ip4_addr_t a; a.addr = addr; esp_ip4addr_ntoa(&a, out, (int)n); }
    else out[0] = 0;
}

/* Find key=value in an application/x-www-form-urlencoded body. Values are
 * simple (IPs/digits); no percent-decoding is performed. */
static bool form_value(const char *body, const char *key, char *out, size_t outlen)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t seg = amp ? (size_t)(amp - p) : strlen(p);
        if (seg > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            size_t vlen = seg - klen - 1;
            if (vlen >= outlen) vlen = outlen - 1;
            memcpy(out, p + klen + 1, vlen);
            out[vlen] = 0;
            return true;
        }
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

static esp_err_t settings_get(httpd_req_t *req)
{
    settings_t s; settings_load(&s);
    char ip[16], mask[16], gw[16], dns[16];
    ip_to_str(s.ip, ip, sizeof(ip));
    ip_to_str(s.netmask, mask, sizeof(mask));
    ip_to_str(s.gateway, gw, sizeof(gw));
    ip_to_str(s.dns, dns, sizeof(dns));
#if CONFIG_AIRSPY_DC_REMOVE
    int dc_avail = 1;
#else
    int dc_avail = 0;
#endif
#if CONFIG_AIRSPY_IQ_BALANCE || CONFIG_AIRSPY_IQ_BALANCE_FREQ
    int iq_avail = 1;
#else
    int iq_avail = 0;
#endif
    char json[300];
    snprintf(json, sizeof(json),
        "{\"net_mode\":%u,\"ip\":\"%s\",\"netmask\":\"%s\",\"gateway\":\"%s\",\"dns\":\"%s\","
        "\"dc_block\":%u,\"iq_balance\":%u,\"dc_available\":%d,\"iq_available\":%d,"
        "\"gain\":%u,\"lna\":%u,\"agc_threshold\":%u}",
        s.net_mode, ip, mask, gw, dns, s.dc_block ? 1 : 0, s.iq_balance ? 1 : 0, dc_avail, iq_avail,
        s.gain, s.lna, s.agc_threshold);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t settings_post(httpd_req_t *req)
{
    char header[8];
    if (httpd_req_get_hdr_value_str(req, "X-Settings", header, sizeof(header)) != ESP_OK || strcmp(header, "1"))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing settings header");
    int total = req->content_len;
    if (total <= 0 || total > 512) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    char body[513];
    int got = 0;
    while (got < total) {
        int n = httpd_req_recv(req, body + got, total - got);
        if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body read failed");
        got += n;
    }
    body[got] = 0;

    settings_t s; settings_load(&s);
    settings_t before = s;
    char val[64];
    esp_ip4_addr_t a;

    if (form_value(body, "net_mode", val, sizeof(val))) {
        unsigned long m = strtoul(val, NULL, 10);
        if (m > 1) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid net_mode");
        s.net_mode = (uint8_t)m;
    }
    if (form_value(body, "ip", val, sizeof(val))) {
        if (!val[0]) s.ip = 0;
        else if (esp_netif_str_to_ip4(val, &a) == ESP_OK) s.ip = a.addr;
        else return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid IP");
    }
    if (form_value(body, "netmask", val, sizeof(val))) {
        if (!val[0]) s.netmask = 0;
        else if (esp_netif_str_to_ip4(val, &a) == ESP_OK) s.netmask = a.addr;
        else return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid netmask");
    }
    if (form_value(body, "gateway", val, sizeof(val))) {
        if (!val[0]) s.gateway = 0;
        else if (esp_netif_str_to_ip4(val, &a) == ESP_OK) s.gateway = a.addr;
        else return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid gateway");
    }
    if (form_value(body, "dns", val, sizeof(val))) {
        if (!val[0]) s.dns = 0;
        else if (esp_netif_str_to_ip4(val, &a) == ESP_OK) s.dns = a.addr;
        else return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid DNS");
    }
    if (form_value(body, "dc_block", val, sizeof(val))) s.dc_block = strtoul(val, NULL, 10) ? 1 : 0;
    if (form_value(body, "iq_balance", val, sizeof(val))) s.iq_balance = strtoul(val, NULL, 10) ? 1 : 0;
    if (form_value(body, "gain", val, sizeof(val))) {
        unsigned long g = strtoul(val, NULL, 10);
        if (g > 9) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid gain");
        s.gain = (uint8_t)g;
    }
    if (form_value(body, "lna", val, sizeof(val))) s.lna = strtoul(val, NULL, 10) ? 1 : 0;
    if (form_value(body, "agc_threshold", val, sizeof(val))) s.agc_threshold = strtoul(val, NULL, 10) ? 1 : 0;

    if (s.net_mode == SETTINGS_NET_STATIC && (!s.ip || !s.netmask))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Static mode needs IP and netmask");

    esp_err_t e = settings_save(&s);
    if (e != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save settings");

    airspy_set_dsp_options(s.dc_block != 0, s.iq_balance != 0);
    airspy_request_gain(s.gain);
    airspy_request_lna(s.lna);
    airspy_request_agc_threshold(s.agc_threshold);

    bool restart = before.net_mode != s.net_mode || before.ip != s.ip || before.netmask != s.netmask ||
                   before.gateway != s.gateway || before.dns != s.dns;
    char json[48];
    snprintf(json, sizeof(json), "{\"saved\":true,\"restart_required\":%s}", restart ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

esp_err_t web_start(esp_eth_handle_t ethernet)
{
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG(); cfg.stack_size=8192; cfg.recv_wait_timeout=15;cfg.max_uri_handlers=20;
    // Serialize history on the network core and give the I/Q sender priority.
    cfg.core_id = 0;
    cfg.task_priority = 3;
    httpd_handle_t server=NULL; esp_err_t e=httpd_start(&server,&cfg); if(e!=ESP_OK) return e;
    const httpd_uri_t routes[]={ {.uri="/",.method=HTTP_GET,.handler=index_get}, {.uri="/api/status",.method=HTTP_GET,.handler=status_get,.user_ctx=ethernet}, {.uri="/api/history",.method=HTTP_GET,.handler=history_get}, {.uri="/api/ota",.method=HTTP_POST,.handler=ota_post}, {.uri="/api/time",.method=HTTP_POST,.handler=time_post}, {.uri="/api/airspy/gain",.method=HTTP_POST,.handler=airspy_gain_post}, {.uri="/api/airspy/verify",.method=HTTP_POST,.handler=airspy_verify_post}, {.uri="/api/airspy/calibration",.method=HTTP_GET,.handler=airspy_calibration_get}, {.uri="/api/airspy/update",.method=HTTP_POST,.handler=airspy_update_post}, {.uri="/api/airspy/update-status",.method=HTTP_GET,.handler=airspy_update_status_get}, {.uri="/api/airspy/recover",.method=HTTP_POST,.handler=airspy_recover_post}, {.uri="/api/restart",.method=HTTP_POST,.handler=restart_post}, {.uri="/api/settings",.method=HTTP_GET,.handler=settings_get}, {.uri="/api/settings",.method=HTTP_POST,.handler=settings_post}, {.uri="/api/factory-reset",.method=HTTP_POST,.handler=factory_reset_post} };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++) if((e=httpd_register_uri_handler(server,&routes[i]))!=ESP_OK) { httpd_stop(server); return e; }
#if CONFIG_AIRSPY_FFT_CAPTURE_TEST
    const httpd_uri_t test={.uri="/api/fft-test",.method=HTTP_GET,.handler=fft_test_get};
    if((e=httpd_register_uri_handler(server,&test))!=ESP_OK) { httpd_stop(server);return e; }
#endif
#if CONFIG_AIRSPY_SPECTRUM_CAPTURE_TEST || CONFIG_AIRSPY_FFT_SERVER
    const httpd_uri_t spectrum_stats={.uri="/api/spectrum-stats",.method=HTTP_GET,.handler=spectrum_stats_get};
    if((e=httpd_register_uri_handler(server,&spectrum_stats))!=ESP_OK) { httpd_stop(server);return e; }
#endif
#if CONFIG_AIRSPY_SPECTRUM_CAPTURE_TEST
    const httpd_uri_t spectrum_frame={.uri="/api/spectrum-frame",.method=HTTP_GET,.handler=spectrum_frame_get};
    if((e=httpd_register_uri_handler(server,&spectrum_frame))!=ESP_OK) { httpd_stop(server);return e; }
#endif
    return ESP_OK;
}
