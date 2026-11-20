#include "history_store.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"

_Static_assert(sizeof(metric_sample_t)==16,"Change persistent format version when layout changes");
#define SLOTS 2048
#define FORMAT 0x48495302u
/* NVS provides the power-loss recovery and data integrity checks. */
typedef struct { uint32_t format,sequence,count; metric_sample_t samples[HISTORY_BATCH_SAMPLES]; } record_t;
typedef struct { uint32_t sequence; unsigned slot; } index_t;
static nvs_handle_t handle;
static uint32_t sequence;
static bool opened;
static void key_for(char *key,unsigned slot) { snprintf(key,12,"b%04u",slot); }
static bool valid(const record_t *r,size_t size)
{
    if(size!=sizeof(*r)||r->format!=FORMAT||!r->count||r->count>HISTORY_BATCH_SAMPLES)return false;
    for(unsigned i=0;i<r->count;i++) {
        if(r->samples[i].uptime<1700000000u || r->samples[i].cpu10>1000 || r->samples[i].ram10>1000 || r->samples[i].psram10>1000 || r->samples[i].flash10>1000)return false;
        if(i && r->samples[i].uptime<=r->samples[i-1].uptime)return false;
    }
    return true;
}
static int compare(const void *a,const void *b)
{
    uint32_t x=((const index_t *)a)->sequence,y=((const index_t *)b)->sequence;
    return x<y?-1:x>y;
}
esp_err_t history_store_open(void (*restore)(const metric_sample_t *,size_t))
{
    esp_err_t e=nvs_flash_init_partition("history");if(e!=ESP_OK)return e;
    if((e=nvs_open_from_partition("history","metrics",NVS_READWRITE,&handle))!=ESP_OK)return e;
    index_t *index=calloc(SLOTS,sizeof(*index));if(!index){nvs_close(handle);return ESP_ERR_NO_MEM;}
    size_t count=0;record_t r;
    for(unsigned i=0;i<SLOTS;i++) {
        char key[12];key_for(key,i);size_t size=sizeof(r);
        if(nvs_get_blob(handle,key,&r,&size)==ESP_OK && valid(&r,size)) { index[count++]=(index_t){r.sequence,i};if(r.sequence>sequence)sequence=r.sequence; }
    }
    qsort(index,count,sizeof(*index),compare);
    for(size_t i=0;i<count;i++) {
        char key[12];key_for(key,index[i].slot);size_t size=sizeof(r);
        if(nvs_get_blob(handle,key,&r,&size)==ESP_OK && valid(&r,size))restore(r.samples,r.count);
    }
    free(index);opened=true;return ESP_OK;
}
esp_err_t history_store_write(const metric_sample_t *samples,size_t count)
{
    if(!opened)return ESP_ERR_INVALID_STATE;
    if(!count || count>HISTORY_BATCH_SAMPLES)return ESP_ERR_INVALID_ARG;
    record_t r={.format=FORMAT,.sequence=sequence+1,.count=count};memcpy(r.samples,samples,count*sizeof(*samples));
    if(!valid(&r,sizeof(r)))return ESP_ERR_INVALID_ARG;
    char key[12];key_for(key,r.sequence%SLOTS);
    esp_err_t e=nvs_set_blob(handle,key,&r,sizeof(r));if(e==ESP_OK)e=nvs_commit(handle);
    if(e==ESP_OK)sequence=r.sequence;
    return e;
}
