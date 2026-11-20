#include <assert.h>
#include <stdio.h>
#include "nvs.h"
#include "settings.h"

static unsigned writes, commits, closes, fail_on_write;
static esp_err_t commit_result;
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle)
{
    assert(name && mode==NVS_READWRITE);
    *handle=1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle==1);closes++; }
esp_err_t nvs_get_u8(nvs_handle_t handle,const char *key,uint8_t *value)
{
    (void)handle;(void)key;(void)value;
    return ESP_FAIL;
}
esp_err_t nvs_get_u32(nvs_handle_t handle,const char *key,uint32_t *value)
{
    (void)handle;(void)key;(void)value;
    return ESP_FAIL;
}
esp_err_t nvs_set_u32(nvs_handle_t handle,const char *key,uint32_t value)
{
    assert(handle==1 && key);
    (void)value;
    return ++writes==fail_on_write?ESP_ERR_NO_MEM:ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t handle,const char *key,uint8_t value)
{
    return nvs_set_u32(handle,key,value);
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle==1);commits++;
    return commit_result;
}
int main(void)
{
    settings_t settings;
    settings_defaults(&settings);
    assert(settings.dc_block==1 && settings.net_mode==SETTINGS_NET_DHCP);
    fail_on_write=3;
    assert(settings_save(&settings)==ESP_ERR_NO_MEM && writes==3 && commits==0 && closes==1);
    fail_on_write=0;
    commit_result=ESP_FAIL;
    assert(settings_save(&settings)==ESP_FAIL && writes==13 && commits==1 && closes==2);
    commit_result=ESP_OK;
    assert(settings_save(&settings)==ESP_OK && writes==23 && commits==2 && closes==3);
    puts("Settings: setter and commit failures propagate and the handle closes");
}
