#include "metrics.h"
#include "history_store.h"
#include "spy_server.h"
#include <time.h>
#include <string.h>
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define FAST_CAP 720
#define DAY_CAP 1440
#define VALID_EPOCH 1700000000u
static metric_sample_t fast[FAST_CAP], day[DAY_CAP],pending[HISTORY_BATCH_SAMPLES];
static size_t fast_pos,fast_count,day_pos,day_count,pending_count;
static uint32_t sum_cpu,sum_ram,sum_psram,sum_flash,sum_net,minute_count,minute_bucket;
static metrics_snapshot_t state;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t storage_lock;

uint32_t metrics_now(void) { time_t now=time(NULL);return now>=VALID_EPOCH?(uint32_t)now:(uint32_t)(esp_timer_get_time()/1000000); }
void metrics_snapshot(metrics_snapshot_t *out) { portENTER_CRITICAL(&lock); *out=state; portEXIT_CRITICAL(&lock); }
/* Called with the snapshot lock held, or during startup before readers exist. */
static void append(metric_sample_t m)
{
    fast[fast_pos]=m;fast_pos=(fast_pos+1)%FAST_CAP;if(fast_count<FAST_CAP)fast_count++;
    uint32_t bucket=m.uptime/60;
    if(!minute_count || bucket!=minute_bucket) {
        minute_bucket=bucket;sum_cpu=sum_ram=sum_psram=sum_flash=sum_net=minute_count=0;
        day_pos=(day_pos+1)%DAY_CAP;if(day_count<DAY_CAP)day_count++;
    }
    sum_cpu+=m.cpu10;sum_ram+=m.ram10;sum_psram+=m.psram10;sum_flash+=m.flash10;sum_net+=m.net_kbps;minute_count++;
    day[(day_pos+DAY_CAP-1)%DAY_CAP]=(metric_sample_t){.uptime=m.uptime,.cpu10=sum_cpu/minute_count,.ram10=sum_ram/minute_count,.psram10=sum_psram/minute_count,.flash10=sum_flash/minute_count,.net_kbps=sum_net/minute_count};
}
static void restore(const metric_sample_t *samples,size_t count) { for(size_t i=0;i<count;i++)append(samples[i]); }
size_t metrics_history(metric_sample_t *out,size_t cap,unsigned seconds)
{
    uint32_t now=metrics_now();size_t result=0;
    portENTER_CRITICAL(&lock);
    const bool daily=seconds>3600;
    size_t count=daily?day_count:fast_count,pos=daily?day_pos:fast_pos,size=daily?DAY_CAP:FAST_CAP;
    const metric_sample_t *src=daily?day:fast;size_t start=(pos+size-count)%size;
    for(size_t i=0;i<count && result<cap;i++) {
        metric_sample_t m=src[(start+i)%size];
        if(m.uptime<=now && now-m.uptime<=seconds)out[result++]=m;
    }
    portEXIT_CRITICAL(&lock);return result;
}
/* Storage mutex ownership is independent of the short RAM snapshot spinlock. */
static esp_err_t flush_locked(void)
{
    if(!pending_count)return ESP_OK;
    esp_err_t e=history_store_write(pending,pending_count);
    portENTER_CRITICAL(&lock);
    if(e==ESP_OK) { state.history_saved=pending[pending_count-1].uptime;pending_count=0; }
    else state.history_errors++;
    portEXIT_CRITICAL(&lock);
    if(e!=ESP_OK)ESP_LOGW("metrics","History save: %s",esp_err_to_name(e));
    return e;
}
esp_err_t metrics_flush(void)
{
    if(!storage_lock || xSemaphoreTake(storage_lock,pdMS_TO_TICKS(5000))!=pdTRUE)return ESP_ERR_TIMEOUT;
    esp_err_t e=flush_locked();xSemaphoreGive(storage_lock);return e;
}
static void sample_task(void *arg)
{
    (void)arg;
    uint32_t prev_idle[portNUM_PROCESSORS];
    for(unsigned i=0;i<portNUM_PROCESSORS;i++)prev_idle[i]=ulTaskGetIdleRunTimeCounterForCore(i);
    int64_t prev_time=esp_timer_get_time();uint64_t prev_bytes=spy_server_bytes_sent();
    TickType_t wake=xTaskGetTickCount();
    for(;;) {
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(5000));
        int64_t now=esp_timer_get_time();uint64_t idle_delta=0,core_idle[portNUM_PROCESSORS];
        for(unsigned i=0;i<portNUM_PROCESSORS;i++) { uint32_t cur=ulTaskGetIdleRunTimeCounterForCore(i);core_idle[i]=(uint32_t)(cur-prev_idle[i]);idle_delta+=core_idle[i];prev_idle[i]=cur; }
        uint64_t elapsed=(uint64_t)(now-prev_time),total=elapsed*portNUM_PROCESSORS;prev_time=now;
        uint64_t bytes=spy_server_bytes_sent();uint32_t net_kbps=elapsed?(uint32_t)(((bytes-prev_bytes)*8000)/elapsed):0;prev_bytes=bytes;
        if(idle_delta>total)idle_delta=total;
        uint32_t cpu=total?(uint32_t)((total-idle_delta)*1000/total):0;
        uint32_t free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),ram=heap_caps_get_total_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),stamp=metrics_now();
        uint32_t psram_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),psram=heap_caps_get_total_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        metric_sample_t m={.uptime=stamp,.cpu10=cpu,.ram10=ram?(uint16_t)(((uint64_t)(ram-free)*1000)/ram):0,.psram10=psram?(uint16_t)(((uint64_t)(psram-psram_free)*1000)/psram):0,.net_kbps=net_kbps,.flash10=state.flash_total?(uint16_t)(((uint64_t)state.flash_reserved*1000)/state.flash_total):0};
        portENTER_CRITICAL(&lock);
        for(unsigned i=0;i<portNUM_PROCESSORS && i<2;i++) { uint64_t idle=core_idle[i]>elapsed?elapsed:core_idle[i];state.core_cpu10[i]=elapsed?(uint16_t)((elapsed-idle)*1000/elapsed):0; }
        state.ram_total=ram;state.ram_free=free;state.latest=m;if(stamp>=VALID_EPOCH)append(m);
        portEXIT_CRITICAL(&lock);
        if(stamp>=VALID_EPOCH && state.history_ready) {
            xSemaphoreTake(storage_lock,portMAX_DELAY);
            if(pending_count==HISTORY_BATCH_SAMPLES)flush_locked();
            if(pending_count<HISTORY_BATCH_SAMPLES && (!pending_count || stamp>pending[pending_count-1].uptime))pending[pending_count++]=m;
            if(pending_count==HISTORY_BATCH_SAMPLES)flush_locked();
            xSemaphoreGive(storage_lock);
        }
    }
}
esp_err_t metrics_start(void)
{
    storage_lock=xSemaphoreCreateMutex();if(!storage_lock)return ESP_ERR_NO_MEM;
    esp_err_t e=esp_flash_get_size(NULL,&state.flash_total);if(e!=ESP_OK)return e;
    esp_partition_iterator_t it=esp_partition_find(ESP_PARTITION_TYPE_ANY,ESP_PARTITION_SUBTYPE_ANY,NULL);
    while(it) { const esp_partition_t *p=esp_partition_get(it);state.flash_reserved+=p->size;it=esp_partition_next(it); }
    const esp_partition_t *p=esp_ota_get_running_partition();
    if(p) { esp_partition_pos_t pos={.offset=p->address,.size=p->size};esp_image_metadata_t image;if(esp_image_get_metadata(&pos,&image)==ESP_OK)state.app_size=image.image_len; }
    e=history_store_open(restore);state.history_ready=e==ESP_OK;
    if(e!=ESP_OK)ESP_LOGW("metrics","Persistent history unavailable: %s",esp_err_to_name(e));
    else if(fast_count)state.history_saved=fast[(fast_pos+FAST_CAP-1)%FAST_CAP].uptime;
    state.ram_total=heap_caps_get_total_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);state.ram_free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    return xTaskCreate(sample_task,"metrics",6144,NULL,3,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
