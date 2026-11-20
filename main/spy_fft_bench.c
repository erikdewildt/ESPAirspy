#include "spy_fft_bench.h"
#include "spy_fft.h"
#include <inttypes.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void benchmark(void *arg)
{
    (void)arg;
    spy_fft_t *f=heap_caps_malloc(sizeof(*f),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    int16_t *iq=heap_caps_malloc(SPY_FFT_SIZE*2*sizeof(*iq),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    uint8_t *bins=heap_caps_malloc(SPY_FFT_SIZE,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    uint8_t *render=heap_caps_malloc(SPY_FFT_SIZE,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!f||!iq||!bins||!render) { ESP_LOGE("fft_bench","Not enough internal RAM");goto done; }
    uint32_t seed=0x12345678;
    for(unsigned i=0;i<SPY_FFT_SIZE*2;i++) { seed=seed*1664525u+1013904223u;iq[i]=(int16_t)(seed>>16); }
    spy_fft_init(f);
    vTaskDelay(pdMS_TO_TICKS(12000));
    for(unsigned round=0;round<2;round++) {
        int64_t total=0,worst=0;uint32_t checksum=0;
        for(unsigned i=0;i<100;i++) {
            int64_t start=esp_timer_get_time();
            spy_fft_compute(f,iq,bins,0,150);
            if(!spy_fft_render(bins,render,SPY_FFT_SIZE,768000,0,660000)) { ESP_LOGE("fft_bench","Render failed");goto done; }
            int64_t elapsed=esp_timer_get_time()-start;total+=elapsed;if(elapsed>worst)worst=elapsed;checksum+=render[i%SPY_FFT_SIZE];
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        ESP_LOGI("fft_bench","round=%u frames=100 average_us=%" PRId64 " worst_us=%" PRId64 " checksum=%" PRIu32,round,total/100,worst,checksum);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
done:
    free(render);free(bins);free(iq);free(f);vTaskDelete(NULL);
}
esp_err_t spy_fft_benchmark_start(void)
{
    return xTaskCreatePinnedToCore(benchmark,"fft_bench",4096,NULL,4,NULL,1)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
