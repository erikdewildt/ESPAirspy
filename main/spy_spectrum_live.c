#include "spy_spectrum_live.h"
#include "spy_spectrum.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* USB preempts the spectrum worker. With the I/Q ring in PSRAM, internal
 * RAM can hold eight queued blocks to cover an FFT plus scheduling jitter. */
#if CONFIG_SPIRAM
#define POOL_SIZE 8
#else
#define POOL_SIZE 4
#endif

typedef struct {
    uint32_t center, rate, sequence, epoch;
    size_t count;
    int16_t iq[SPY_SPECTRUM_CHUNK_MAX * 2];
} block_t;
typedef struct {
    QueueHandle_t available, ready, results;
    block_t *pool[POOL_SIZE];
    spy_spectrum_t *spectrum;
    spy_spectrum_live_frame_t *result;
    /* Low three bits are the stage; remaining bits are a generation. */
    atomic_uint next_sequence, config, submitted, processed, dropped, resets, frames, max_process_us;
    atomic_bool enabled;
} live_t;
static _Atomic(live_t *) live;

static void worker(void *arg)
{
    live_t *s=arg;
    bool have_previous=false;
    uint32_t last_sequence=0,last_epoch=0,last_center=0,last_rate=0,frame_sequence=0;
    spy_spectrum_init(s->spectrum);
    for (;;) {
        block_t *b;
        if(xQueueReceive(s->ready,&b,portMAX_DELAY)!=pdTRUE)continue;
        int64_t started=esp_timer_get_time();
        if(!have_previous || b->sequence!=last_sequence+1 || b->epoch!=last_epoch || b->center!=last_center || b->rate!=last_rate) {
            if(have_previous)atomic_fetch_add(&s->resets,1);
            have_previous=spy_spectrum_configure(s->spectrum,b->rate,b->epoch&7u,b->center);
        }
        last_sequence=b->sequence;last_epoch=b->epoch;last_center=b->center;last_rate=b->rate;
        if(have_previous && spy_spectrum_process_db(s->spectrum,b->iq,b->count,b->center,s->result->bins_db)) {
            s->result->center_hz=b->center;s->result->sample_rate=b->rate>>(b->epoch&7u);
            s->result->stage=b->epoch&7u;s->result->sequence=frame_sequence++;s->result->epoch=b->epoch;
            xQueueOverwrite(s->results,s->result);atomic_fetch_add(&s->frames,1);
        }
        /* Wall-clock time for this block, including any preemption by the
         * higher-priority USB task. It is not a pure FFT/CPU cost; the FFT is
         * amortised (a few frames per second) and the per-block decimation
         * dominates. See docs/ for the FFT/spectrum re-assessment. */
        unsigned elapsed=(unsigned)(esp_timer_get_time()-started);
        unsigned previous=atomic_load(&s->max_process_us);
        if(elapsed>previous)atomic_store(&s->max_process_us,elapsed);
        atomic_fetch_add(&s->processed,1);
        /* A received block owns exactly one slot in this pool. */
        BaseType_t returned=xQueueSend(s->available,&b,0);
        configASSERT(returned==pdTRUE);(void)returned;
    }
}
esp_err_t spy_spectrum_live_start(unsigned stage)
{
    if(stage>IQ_DECIMATOR_MAX_STAGES)return ESP_ERR_INVALID_ARG;
    if(atomic_load(&live))return ESP_ERR_INVALID_STATE;
    live_t *s=calloc(1,sizeof(*s));if(!s)return ESP_ERR_NO_MEM;
    atomic_init(&s->next_sequence,0);atomic_init(&s->config,stage);atomic_init(&s->submitted,0);
    atomic_init(&s->processed,0);atomic_init(&s->dropped,0);atomic_init(&s->resets,0);
    atomic_init(&s->frames,0);atomic_init(&s->max_process_us,0);
    atomic_init(&s->enabled,true);
    s->available=xQueueCreate(POOL_SIZE,sizeof(block_t *));s->ready=xQueueCreate(POOL_SIZE,sizeof(block_t *));
    s->results=xQueueCreate(1,sizeof(spy_spectrum_live_frame_t));
    for(unsigned i=0;i<POOL_SIZE;i++)s->pool[i]=heap_caps_malloc(sizeof(block_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    s->spectrum=heap_caps_malloc(sizeof(spy_spectrum_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    s->result=heap_caps_malloc(sizeof(spy_spectrum_live_frame_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!s->available || !s->ready || !s->results || !s->spectrum || !s->result)goto fail;
    for(unsigned i=0;i<POOL_SIZE;i++)if(!s->pool[i])goto fail;
    for(unsigned i=0;i<POOL_SIZE;i++)xQueueSend(s->available,&s->pool[i],0);
    if(xTaskCreatePinnedToCore(worker,"spectrum",4096,s,4,NULL,1)!=pdPASS)goto fail;
    atomic_store(&live,s);return ESP_OK;
fail:
    if(s->available)vQueueDelete(s->available);
    if(s->ready)vQueueDelete(s->ready);
    if(s->results)vQueueDelete(s->results);
    for(unsigned i=0;i<POOL_SIZE;i++)free(s->pool[i]);
    free(s->spectrum);free(s->result);free(s);return ESP_ERR_NO_MEM;
}
void spy_spectrum_live_set_enabled(bool enabled)
{
    live_t *s=atomic_load(&live);if(!s)return;
    bool old=atomic_exchange(&s->enabled,enabled);
    if(old!=enabled)atomic_fetch_add(&s->config,8);
}
bool spy_spectrum_live_set_stage(unsigned stage)
{
    live_t *s=atomic_load(&live);if(!s || stage>IQ_DECIMATOR_MAX_STAGES)return false;
    unsigned old=atomic_load(&s->config);
    for(;;) {
        if((old&7u)==stage)return true;
        unsigned next=((old+8u)&~7u)|stage;
        if(atomic_compare_exchange_weak(&s->config,&old,next))return true;
    }
}
void spy_spectrum_live_submit(const int16_t *iq,size_t samples,uint32_t center,uint32_t rate)
{
    live_t *s=atomic_load(&live);if(!s || !atomic_load(&s->enabled))return;
    uint32_t seq=atomic_fetch_add(&s->next_sequence,1);
    block_t *b;
    if(!iq || !samples || samples>SPY_SPECTRUM_CHUNK_MAX || xQueueReceive(s->available,&b,0)!=pdTRUE) {
        atomic_fetch_add(&s->dropped,1);atomic_fetch_add(&s->config,8);return;
    }
    b->center=center;b->rate=rate;b->sequence=seq;b->epoch=atomic_load(&s->config);b->count=samples;
    memcpy(b->iq,iq,samples*2*sizeof(*iq));
    if(xQueueSend(s->ready,&b,0)!=pdTRUE) {
        atomic_fetch_add(&s->dropped,1);atomic_fetch_add(&s->config,8);
        BaseType_t returned=xQueueSend(s->available,&b,0);
        configASSERT(returned==pdTRUE);(void)returned;return;
    }
    atomic_fetch_add(&s->submitted,1);
}
void spy_spectrum_live_discontinuity(void)
{
    live_t *s=atomic_load(&live);if(s)atomic_fetch_add(&s->config,8);
}
bool spy_spectrum_live_take(spy_spectrum_live_frame_t *frame)
{
    live_t *s=atomic_load(&live);
    return s && atomic_load(&s->enabled) && frame && xQueueReceive(s->results,frame,0)==pdTRUE && frame->epoch==atomic_load(&s->config);
}
void spy_spectrum_live_stats(spy_spectrum_live_stats_t *stats)
{
    if(!stats)return;
    live_t *s=atomic_load(&live);if(!s){*stats=(spy_spectrum_live_stats_t){0};return;}
    *stats=(spy_spectrum_live_stats_t){atomic_load(&s->submitted),atomic_load(&s->processed),atomic_load(&s->dropped),atomic_load(&s->resets),atomic_load(&s->frames),atomic_load(&s->max_process_us)};
}
