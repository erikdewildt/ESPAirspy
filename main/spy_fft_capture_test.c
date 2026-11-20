#include "spy_fft_capture_test.h"
#include "spy_fft_live.h"
#include <inttypes.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static void monitor(void *arg)
{
    (void)arg;
    int64_t until=esp_timer_get_time()+50000000,last=0;
    while(esp_timer_get_time()<until) {
        if(esp_timer_get_time()-last>=2000000) {
            spy_fft_live_stats_t s;spy_fft_live_stats(&s);
            ESP_LOGI("fft_live_test","submitted=%" PRIu32 " completed=%" PRIu32 " skipped=%" PRIu32,s.submitted,s.completed,s.skipped);
            last=esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelete(NULL);
}
esp_err_t spy_fft_capture_test_start(void)
{
    return xTaskCreatePinnedToCore(monitor,"fft_monitor",3072,NULL,3,NULL,1)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
