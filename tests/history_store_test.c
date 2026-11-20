#include "history_store.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char records[2048][256];
static size_t sizes[2048];
static bool fail_write;
static uint32_t last;
static unsigned restored;
esp_err_t nvs_flash_init_partition(const char *name){assert(!strcmp(name,"history"));return ESP_OK;}
esp_err_t nvs_open_from_partition(const char *p,const char *n,int m,nvs_handle_t *h){(void)p;(void)n;(void)m;*h=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
static unsigned slot(const char *key){unsigned s=(unsigned)strtoul(key+1,NULL,10);assert(s<2048);return s;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size){(void)h;unsigned s=slot(key);if(!sizes[s]||*size<sizes[s])return ESP_FAIL;memcpy(out,records[s],sizes[s]);*size=sizes[s];return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t size){(void)h;if(fail_write)return ESP_FAIL;unsigned s=slot(key);assert(size<=256);memcpy(records[s],in,size);sizes[s]=size;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return ESP_OK;}
static void restore(const metric_sample_t *m,size_t n){for(size_t i=0;i<n;i++){assert(m[i].uptime>last);last=m[i].uptime;restored++;}}
static void scan(void){last=0;restored=0;assert(history_store_open(restore)==ESP_OK);}
int main(void)
{
    scan();assert(restored==0);
    metric_sample_t m={.cpu10=100,.ram10=300,.flash10=800};
    for(unsigned i=1;i<=4096;i++){m.uptime=1700000000u+i*60;assert(history_store_write(&m,1)==ESP_OK);}
    scan();assert(restored==2048 && last==m.uptime);
    fail_write=true;m.uptime+=60;assert(history_store_write(&m,1)!=ESP_OK);
    scan();assert(restored==2048 && last==m.uptime-60);
    fail_write=false;assert(history_store_write(&m,1)==ESP_OK);
    scan();assert(restored==2048 && last==m.uptime);
    records[100][0]^=1;scan();assert(restored==2047);
    m.uptime=1;assert(history_store_write(&m,1)==ESP_ERR_INVALID_ARG);
    assert(history_store_write(&m,13)==ESP_ERR_INVALID_ARG);
    puts("History store: ring wrap, chronological restore, failed write, incompatible format and input bounds passed");
}
