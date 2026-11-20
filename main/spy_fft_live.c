#include "spy_fft_live.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef struct { uint32_t center_hz,sequence;int16_t iq[SPY_FFT_SIZE*2]; } snapshot_t;
typedef struct {
    QueueHandle_t available,ready,results;
    snapshot_t *pool[2];spy_fft_t *workspace;spy_fft_frame_t *result;
    atomic_uint submitted,completed,skipped;
} live_t;
static _Atomic(live_t *) live;
static int64_t last_capture_us;

static void worker(void *arg)
{
    live_t *s=arg;spy_fft_init(s->workspace);
    for(;;) {
        snapshot_t *frame;
        if(xQueueReceive(s->ready,&frame,portMAX_DELAY)!=pdTRUE)continue;
        s->result->center_hz=frame->center_hz;s->result->sequence=frame->sequence;
        spy_fft_compute(s->workspace,frame->iq,s->result->bins,0,150);
#if CONFIG_AIRSPY_FFT_CAPTURE_TEST
        memcpy(s->result->test_iq,frame->iq,sizeof(frame->iq));
#endif
        xQueueOverwrite(s->results,s->result);
        atomic_fetch_add(&s->completed,1);
        xQueueSend(s->available,&frame,portMAX_DELAY);
    }
}
esp_err_t spy_fft_live_start(void)
{
    if(atomic_load(&live))return ESP_ERR_INVALID_STATE;
    live_t *s=calloc(1,sizeof(*s));if(!s)return ESP_ERR_NO_MEM;
    atomic_init(&s->submitted,0);atomic_init(&s->completed,0);atomic_init(&s->skipped,0);
    s->available=xQueueCreate(2,sizeof(snapshot_t *));s->ready=xQueueCreate(2,sizeof(snapshot_t *));
    s->results=xQueueCreate(1,sizeof(spy_fft_frame_t));
    s->pool[0]=heap_caps_malloc(sizeof(snapshot_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    s->pool[1]=heap_caps_malloc(sizeof(snapshot_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    s->workspace=heap_caps_malloc(sizeof(spy_fft_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    s->result=heap_caps_malloc(sizeof(spy_fft_frame_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!s->available||!s->ready||!s->results||!s->pool[0]||!s->pool[1]||!s->workspace||!s->result)goto fail;
    xQueueSend(s->available,&s->pool[0],0);xQueueSend(s->available,&s->pool[1],0);
    if(xTaskCreatePinnedToCore(worker,"fft_live",4096,s,4,NULL,1)!=pdPASS)goto fail;
    atomic_store(&live,s);return ESP_OK;
fail:
    if(s->available)vQueueDelete(s->available);
    if(s->ready)vQueueDelete(s->ready);
    if(s->results)vQueueDelete(s->results);
    free(s->pool[0]);free(s->pool[1]);free(s->workspace);free(s->result);free(s);return ESP_ERR_NO_MEM;
}
void spy_fft_live_submit(const int16_t *iq,size_t samples,uint32_t center_hz)
{
    live_t *s=atomic_load(&live);if(!s || samples<SPY_FFT_SIZE)return;
    int64_t now=esp_timer_get_time();if(now-last_capture_us<100000)return;last_capture_us=now;
    snapshot_t *frame;
    if(xQueueReceive(s->available,&frame,0)!=pdTRUE){atomic_fetch_add(&s->skipped,1);return;}
    frame->center_hz=center_hz;frame->sequence=atomic_fetch_add(&s->submitted,1);
    memcpy(frame->iq,iq,sizeof(frame->iq));
    if(xQueueSend(s->ready,&frame,0)!=pdTRUE){atomic_fetch_add(&s->skipped,1);xQueueSend(s->available,&frame,0);}
}
bool spy_fft_live_take(spy_fft_frame_t *frame) { live_t *s=atomic_load(&live);return s && xQueueReceive(s->results,frame,0)==pdTRUE; }
void spy_fft_live_stats(spy_fft_live_stats_t *out)
{
    live_t *s=atomic_load(&live);if(!s){*out=(spy_fft_live_stats_t){0};return;}
    *out=(spy_fft_live_stats_t){atomic_load(&s->submitted),atomic_load(&s->completed),atomic_load(&s->skipped)};
}
