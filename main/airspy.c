#include "airspy.h"
#include "airspy_update.h"
#include "iq_mixer.h"
#include "airspy_tuning.h"
#include "airspy_signal.h"
#if CONFIG_AIRSPY_IQ_BALANCE
#include "iq_balance.h"
#endif
#if CONFIG_AIRSPY_IQ_BALANCE_FREQ
#include "iq_balance_freq.h"
#endif
#if CONFIG_AIRSPY_DC_REMOVE
#include "dc_block.h"
#endif
#if CONFIG_AIRSPY_SPECTRUM_CAPTURE_TEST || CONFIG_AIRSPY_FFT_SERVER
#include "spy_spectrum_live.h"
#endif
#if CONFIG_AIRSPY_FFT_CAPTURE_TEST
#include "spy_fft_live.h"
#endif
#include <math.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#define BULK_SIZE 16384
#define BULK_COUNT 4
#define INTERNAL_RING_BYTES (128 * 1024)
#define EXTERNAL_RING_BYTES (512 * 1024)
#define AIRSPY_VID 0x03eb
#define AIRSPY_PID 0x800c
#define AIRSPY_SAMBA_PID 0x6124
static const char *TAG="airspy_usb";
static usb_host_client_handle_t client;
static usb_device_handle_t device;
static usb_device_handle_t samba_device;
static uint8_t samba_bulk_in, samba_bulk_out;
static uint16_t samba_bulk_mps;
static volatile bool samba_bulk_done;
static usb_transfer_status_t samba_bulk_status;
static int samba_bulk_actual;
static StreamBufferHandle_t iq_ring;
static atomic_bool connected, streaming, writer_busy;
static atomic_uint rate_hz, request_hz, tuned_hz, dropped, received, completions, empty;
static atomic_uint requested_gain, applied_gain;
static atomic_uint requested_lna, applied_lna;
static atomic_uint requested_thresh, applied_thresh;
static uint8_t new_address;
static bool control_done;
static usb_transfer_status_t control_status;
static int control_actual;
static uint32_t rate_index;
static iq_mixer_t mixer;
#if CONFIG_AIRSPY_IQ_BALANCE
static iq_balance_t balance;
#endif
#if CONFIG_AIRSPY_IQ_BALANCE_FREQ
static iq_balance_freq_t balance_freq;
#endif
#if CONFIG_AIRSPY_DC_REMOVE
static dc_block_t dc_block;
#endif
/* Runtime experiment toggles (defaults set in airspy_start, overridden from
 * persistent settings). dsp_reset re-initialises filter state at a block
 * boundary after a toggle so a re-enable starts clean. */
static atomic_bool dsp_dc,dsp_iq,dsp_reset;
static uint32_t lo_hz;
static double frequency_delta;
static atomic_int calibration_ppb;
static atomic_bool calibration_valid;
static char firmware_version[64];
static char serial_number[17];
static uint8_t factory_configuration[256];
static atomic_bool configuration_available;
static atomic_uint actual_lo_hz;
static portMUX_TYPE input_stats_lock=portMUX_INITIALIZER_UNLOCKED;
static uint64_t input_samples,input_near_rail,mixer_clipped,ring_lost_samples;
static size_t ring_capacity,ring_peak_estimate;
void airspy_iq_buffer_stats(uint64_t *lost_samples,size_t *capacity,size_t *peak_estimate)
{
    portENTER_CRITICAL(&input_stats_lock);
    *lost_samples=ring_lost_samples;*capacity=ring_capacity;*peak_estimate=ring_peak_estimate;
    portEXIT_CRITICAL(&input_stats_lock);
}
void airspy_input_level_counts(uint64_t *samples,uint64_t *near_rail,uint64_t *mixed_clips)
{
    portENTER_CRITICAL(&input_stats_lock);*samples=input_samples;*near_rail=input_near_rail;*mixed_clips=mixer_clipped;portEXIT_CRITICAL(&input_stats_lock);
}

int32_t airspy_calibration_ppb(void) { return atomic_load(&calibration_ppb); }
bool airspy_calibration_valid(void) { return atomic_load(&calibration_valid); }
uint32_t airspy_lo_frequency(void) { return atomic_load(&actual_lo_hz); }

bool airspy_connected(void) { return atomic_load(&connected); }
bool airspy_iq_streaming(void) { return atomic_load(&streaming); }
const char *airspy_firmware_version(void) { return atomic_load(&connected)?firmware_version:""; }
const char *airspy_serial_number(void) { return atomic_load(&connected)?serial_number:""; }
bool airspy_configuration_backup(uint8_t out[256])
{
    if(!out || !atomic_load(&connected) || !atomic_load(&configuration_available))return false;
    memcpy(out,factory_configuration,sizeof(factory_configuration));return true;
}
void airspy_request_gain(uint32_t gain) { atomic_store(&requested_gain,gain); }
uint32_t airspy_gain(void) { return atomic_load(&applied_gain); }
void airspy_request_lna(uint32_t lna) { atomic_store(&requested_lna,lna?1:0); }
uint32_t airspy_lna(void) { return atomic_load(&applied_lna); }
void airspy_request_agc_threshold(uint32_t high) { atomic_store(&requested_thresh,high?1:0); }
uint32_t airspy_agc_threshold(void) { return atomic_load(&applied_thresh); }
uint32_t airspy_sample_rate(void) { return atomic_load(&rate_hz); }
uint32_t airspy_frequency(void) { return atomic_load(&tuned_hz); }
uint32_t airspy_dropped_blocks(void) { return atomic_load(&dropped); }
uint32_t airspy_received_blocks(void) { return atomic_load(&received); }
uint32_t airspy_usb_completions(void) { return atomic_load(&completions); }
uint32_t airspy_usb_empty(void) { return atomic_load(&empty); }
void airspy_request_frequency(uint32_t hz) { atomic_store(&request_hz,hz); }
void airspy_set_streaming(bool on)
{
#if CONFIG_AIRSPY_SPECTRUM_CAPTURE_TEST || CONFIG_AIRSPY_FFT_SERVER
    if(on)spy_spectrum_live_discontinuity();
#endif
    atomic_store(&streaming,on);
    /* After disabling, wait for a producer that had already observed true.
     * Later callbacks see false and cannot append to the stream buffer. */
    if(!on)while(atomic_load(&writer_busy))vTaskDelay(1);
}
size_t airspy_read(void *p,size_t len,TickType_t wait) { return xStreamBufferReceive(iq_ring,p,len,wait); }
void airspy_set_dsp_options(bool dc_block,bool iq_balance)
{
#if CONFIG_AIRSPY_DC_REMOVE
    atomic_store(&dsp_dc,dc_block);
#endif
#if CONFIG_AIRSPY_IQ_BALANCE || CONFIG_AIRSPY_IQ_BALANCE_FREQ
    atomic_store(&dsp_iq,iq_balance);
#endif
    (void)dc_block; (void)iq_balance;
    atomic_store(&dsp_reset,true);
}

static void usb_event(const usb_host_client_event_msg_t *msg,void *arg)
{
    (void)arg;
    if(msg->event==USB_HOST_CLIENT_EVENT_NEW_DEV) { ESP_LOGI(TAG,"USB NEW_DEV addr %u",msg->new_dev.address); new_address=msg->new_dev.address; }
    if(msg->event==USB_HOST_CLIENT_EVENT_DEV_GONE) {
        ESP_LOGI(TAG,"USB DEV_GONE");
        if(msg->dev_gone.dev_hdl==device) {
            atomic_store(&connected,false); device=NULL;
            if(!airspy_update_in_progress()) { ESP_LOGW(TAG,"Airspy removed; restarting USB host"); esp_restart(); }
            else ESP_LOGW(TAG,"Airspy left the bus during an update");
        }
        if(msg->dev_gone.dev_hdl==samba_device) { samba_device=NULL; ESP_LOGW(TAG,"SAM-BA bootloader left the bus"); }
    }
}
static void control_cb(usb_transfer_t *t) { control_status=t->status; control_actual=t->actual_num_bytes; control_done=true; }
static esp_err_t control_transfer_dev(usb_device_handle_t dev,uint8_t type,uint8_t request,uint16_t value,uint16_t index,void *data,uint16_t len,uint16_t *actual)
{
    if(actual)*actual=0;
    usb_transfer_t *t=NULL;
    esp_err_t err=usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE+len,0,&t);
    if(err!=ESP_OK) return err;
    usb_setup_packet_t *s=(usb_setup_packet_t *)t->data_buffer;
    *s=(usb_setup_packet_t){.bmRequestType=type,.bRequest=request,.wValue=value,.wIndex=index,.wLength=len};
    if(!(type&0x80) && len) memcpy(t->data_buffer+USB_SETUP_PACKET_SIZE,data,len);
    t->num_bytes=USB_SETUP_PACKET_SIZE+len; t->device_handle=dev; t->bEndpointAddress=type&0x80; t->callback=control_cb;
    control_done=false;
    err=usb_host_transfer_submit_control(client,t);
    if(err==ESP_OK) {
        int64_t deadline=esp_timer_get_time()+3000000;
        while(!control_done && esp_timer_get_time()<deadline) usb_host_client_handle_events(client,pdMS_TO_TICKS(20));
        if(!control_done) { ESP_LOGE(TAG,"USB control timeout; restarting"); esp_restart(); }
        int payload=control_actual-USB_SETUP_PACKET_SIZE;
        if(control_status!=USB_TRANSFER_STATUS_COMPLETED || payload<0 || payload>len || (!actual && payload!=len)) err=ESP_FAIL;
        else {
            if((type&0x80) && payload)memcpy(data,t->data_buffer+USB_SETUP_PACKET_SIZE,payload);
            if(actual)*actual=(uint16_t)payload;
        }
    }
    usb_host_transfer_free(t);
    return err;
}
static esp_err_t control_transfer(uint8_t type,uint8_t request,uint16_t value,uint16_t index,void *data,uint16_t len,uint16_t *actual)
{
    return control_transfer_dev(device,type,request,value,index,data,len,actual);
}
static esp_err_t control(uint8_t type,uint8_t request,uint16_t value,uint16_t index,void *data,uint16_t len)
{
    return control_transfer(type,request,value,index,data,len,NULL);
}
static esp_err_t samba_control(uint8_t type,uint8_t request,uint16_t value,uint16_t index,void *data,uint16_t len)
{
    return control_transfer_dev(samba_device,type,request,value,index,data,len,NULL);
}
static void bulk_cb(usb_transfer_t *t)
{
    atomic_fetch_add(&completions,1);
    if(t->status==USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes==0) atomic_fetch_add(&empty,1);
    if(t->status==USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes>0) {
        size_t n=(size_t)t->actual_num_bytes;
        unsigned block=atomic_fetch_add(&received,1);
        if(block==0) ESP_LOGI(TAG,"First USB block: %d bytes",t->actual_num_bytes);
        if(n%4) { ESP_LOGE(TAG,"Misaligned sample block"); esp_restart(); }
        atomic_store(&writer_busy,true);
        if(atomic_load(&streaming)) {
            uint32_t near=airspy_iq_near_rail((const int16_t *)t->data_buffer,n/4);
            if(atomic_exchange(&dsp_reset,false)) {
#if CONFIG_AIRSPY_DC_REMOVE
                dc_block_init(&dc_block);
#endif
#if CONFIG_AIRSPY_IQ_BALANCE
                iq_balance_init(&balance);
#endif
#if CONFIG_AIRSPY_IQ_BALANCE_FREQ
                iq_balance_freq_reset(&balance_freq);
#endif
            }
#if CONFIG_AIRSPY_DC_REMOVE
            if(atomic_load(&dsp_dc)) dc_block_process(&dc_block,(int16_t *)t->data_buffer,n/4);
#endif
#if CONFIG_AIRSPY_IQ_BALANCE
            if(atomic_load(&dsp_iq)) iq_balance_process(&balance,(int16_t *)t->data_buffer,n/4);
#endif
#if CONFIG_AIRSPY_IQ_BALANCE_FREQ
            if(atomic_load(&dsp_iq)) iq_balance_freq_process(&balance_freq,(int16_t *)t->data_buffer,n/4);
#endif
            uint32_t before=mixer.clipped_components;
            iq_mixer_process(&mixer,(int16_t *)t->data_buffer,n/4);
#if CONFIG_AIRSPY_SPECTRUM_CAPTURE_TEST || CONFIG_AIRSPY_FFT_SERVER
            spy_spectrum_live_submit((const int16_t *)t->data_buffer,n/4,atomic_load(&tuned_hz),atomic_load(&rate_hz));
#endif
#if CONFIG_AIRSPY_FFT_CAPTURE_TEST
            spy_fft_live_submit((const int16_t *)t->data_buffer,n/4,atomic_load(&tuned_hz));
#endif
            uint32_t clipped=mixer.clipped_components-before;
            size_t space=xStreamBufferSpacesAvailable(iq_ring);
            size_t queued=space<n?0:xStreamBufferSend(iq_ring,t->data_buffer,n,0);
            if(queued!=n)atomic_fetch_add(&dropped,1);
            /* The consumer can drain while the copy is in progress. Use the
             * pre-send sample plus accepted bytes as an upper estimate. */
            size_t occupancy=ring_capacity-space+queued;
            portENTER_CRITICAL(&input_stats_lock);
            input_samples+=n/4;input_near_rail+=near;mixer_clipped+=clipped;
            ring_lost_samples+=(n-queued)/4;
            if(occupancy>ring_peak_estimate)ring_peak_estimate=occupancy;
            portEXIT_CRITICAL(&input_stats_lock);
        }
        atomic_store(&writer_busy,false);
    } else if(t->status!=USB_TRANSFER_STATUS_COMPLETED) {
        /* During an update the receiver reboots into its bootloader, which
         * cancels the in-flight streaming transfers. Drop them silently. */
        if(airspy_update_in_progress()) return;
        ESP_LOGE(TAG,"USB stream error %d",t->status); esp_restart();
    }
    if(usb_host_transfer_submit(t)!=ESP_OK) { ESP_LOGE(TAG,"USB stream resubmit failed"); esp_restart(); }
}
static esp_err_t tune(uint32_t hz)
{
    double adjusted=airspy_adjusted_frequency(hz,atomic_load(&calibration_ppb));
    uint32_t khz=(uint32_t)lround(adjusted/1000.0);
    if(khz<180) khz=180;
    uint8_t buf[4]={khz>>24,khz>>16,khz>>8,khz};
    esp_err_t e=lo_hz==khz*1000?ESP_OK:control(0x40,2,0,0,buf,sizeof(buf));
    if(e==ESP_OK) {
        if(lo_hz!=khz*1000) {
            uint8_t delta[4]={0};uint16_t size=0;frequency_delta=0;
            if(control_transfer(0xc0,16,0,0,delta,sizeof(delta),&size)==ESP_OK)
                airspy_parse_frequency_delta(delta,size,&frequency_delta);
        }
        lo_hz=khz*1000;atomic_store(&actual_lo_hz,lo_hz);
        iq_mixer_set(&mixer,adjusted-lo_hz+frequency_delta,atomic_load(&rate_hz));
        atomic_store(&tuned_hz,hz);ESP_LOGI(TAG,"Center %" PRIu32 " Hz, LO %" PRIu32 " Hz",hz,lo_hz);
    }
    return e;
}
/* Index 0 is receiver AGC. Indices 1..9 select attenuation 48..0 dB. */
static esp_err_t set_gain(uint32_t gain)
{
    if(gain>AIRSPY_GAIN_MAX)return ESP_ERR_INVALID_ARG;
    esp_err_t e=control(0x40,10,gain==0,0,NULL,0);
    if(e==ESP_OK && gain)e=control(0x40,12,AIRSPY_GAIN_MAX-gain,0,NULL,0);
    if(e==ESP_OK) { atomic_store(&applied_gain,gain);ESP_LOGI(TAG,"Receiver gain index %" PRIu32,gain); }
    return e;
}
/* SET_LNA (request 13): 0 = off, 1 = +6 dB postamp. */
static esp_err_t set_lna(uint32_t lna)
{
    esp_err_t e=control(0x40,13,lna?1:0,0,NULL,0);
    if(e==ESP_OK) { atomic_store(&applied_lna,lna?1:0);ESP_LOGI(TAG,"Receiver LNA %" PRIu32,lna?1:0); }
    return e;
}
/* SET_AGC_THRESHOLD (request 11): 0 = low, 1 = high. */
static esp_err_t set_agc_threshold(uint32_t high)
{
    esp_err_t e=control(0x40,11,high?1:0,0,NULL,0);
    if(e==ESP_OK) { atomic_store(&applied_thresh,high?1:0);ESP_LOGI(TAG,"AGC threshold %" PRIu32,high?1:0); }
    return e;
}
static esp_err_t open_receiver(uint8_t addr)
{
    esp_err_t e=usb_host_device_open(client,addr,&device); if(e!=ESP_OK) return e;
    const usb_device_desc_t *d=NULL;
    e=usb_host_get_device_descriptor(device,&d);
    if(e!=ESP_OK || d->idVendor!=AIRSPY_VID || d->idProduct!=AIRSPY_PID) { usb_host_device_close(client,device); device=NULL; return ESP_ERR_NOT_SUPPORTED; }
    usb_device_info_t devinfo;
    if(usb_host_device_info(device,&devinfo)==ESP_OK) ESP_LOGI(TAG,"Airspy HF+ detected, USB speed %d",devinfo.speed);
    e=usb_host_interface_claim(client,device,0,1); if(e!=ESP_OK) return e;
    // The sample endpoint exists on alternate setting 1; claiming it on the host
    // does not itself send SET_INTERFACE to the receiver.
    if((e=control(0x01,USB_B_REQUEST_SET_INTERFACE,1,0,NULL,0))!=ESP_OK) return e;
    uint8_t alt=255; if((e=control(0x81,USB_B_REQUEST_GET_INTERFACE,0,0,&alt,1))!=ESP_OK || alt!=1) { ESP_LOGE(TAG,"Unexpected alternate setting %u",alt); return ESP_FAIL; }
    ESP_LOGI(TAG,"USB interface alternate setting %u",alt);
    vTaskDelay(pdMS_TO_TICKS(100));
    /* libairspyhf GET_VERSION_STRING, vendor IN request 9, at most 63 bytes.
     * Allow only printable version characters so it is safe in status JSON. */
    uint8_t version_raw[63]={0};uint16_t version_length=0;
    firmware_version[0]=0;
    if(control_transfer(0xc0,9,0,0,version_raw,sizeof(version_raw),&version_length)==ESP_OK) {
        size_t n=0;
        while(n<version_length && version_raw[n]) {
            unsigned char ch=version_raw[n];
            if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='.'||ch=='-'||ch=='_'||ch==' '))break;
            firmware_version[n]=(char)ch;n++;
        }
        firmware_version[n]=0;
        ESP_LOGI(TAG,"Airspy firmware: %s",n?firmware_version:"unavailable");
    } else ESP_LOGW(TAG,"Airspy firmware version unavailable");
    uint32_t identity[5]={0};serial_number[0]=0;
    if(control(0xc0,7,0,0,identity,sizeof(identity))==ESP_OK)
        snprintf(serial_number,sizeof(serial_number),"%08" PRIX32 "%08" PRIX32,identity[1],identity[2]);
    uint8_t config[256];int32_t ppb=0;
    atomic_store(&configuration_available,false);
    if(control(0xc0,5,0,0,config,sizeof(config))==ESP_OK && airspy_parse_calibration(config,sizeof(config),&ppb)) {
        memcpy(factory_configuration,config,sizeof(config));atomic_store(&configuration_available,true);
        atomic_store(&calibration_ppb,ppb);atomic_store(&calibration_valid,true);
        ESP_LOGI(TAG,"Factory oscillator calibration: %" PRId32 " ppb",ppb);
    } else ESP_LOGW(TAG,"Factory calibration unavailable; using 0 ppb");
    uint32_t count=0,rates[32]={0};
    e=control(0xc0,3,0,0,&count,sizeof(count));
    if(e!=ESP_OK || count==0 || count>32) { ESP_LOGE(TAG,"Invalid sample-rate list count=%" PRIu32,count); return ESP_FAIL; }
    e=control(0xc0,3,0,count,rates,count*sizeof(uint32_t)); if(e!=ESP_OK) return e;
    rate_index=0; for(uint32_t i=0;i<count;i++) if(rates[i]==768000) rate_index=i;
    atomic_store(&rate_hz,rates[rate_index]);
    ESP_LOGI(TAG,"Sample rate %" PRIu32 " (index %" PRIu32 ")",rates[rate_index],rate_index);
    if((e=control(0x40,1,0,0,NULL,0))!=ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(50));
    if((e=control(0x40,4,0,rate_index,NULL,0))!=ESP_OK) return e;
    if((e=set_gain(atomic_load(&requested_gain)))!=ESP_OK) return e;
    if((e=set_lna(atomic_load(&requested_lna)))!=ESP_OK) return e;
    if((e=set_agc_threshold(atomic_load(&requested_thresh)))!=ESP_OK) return e;
    if((e=tune(atomic_load(&request_hz)))!=ESP_OK) return e;
    // Clear endpoint halt, matching libairspyhf before starting the receiver.
    if((e=control(0x02,USB_B_REQUEST_CLEAR_FEATURE,0,0x81,NULL,0))!=ESP_OK) return e;
    if((e=control(0x40,1,1,0,NULL,0))!=ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(100));
    for(unsigned i=0;i<BULK_COUNT;i++) {
        usb_transfer_t *t=NULL; if((e=usb_host_transfer_alloc(BULK_SIZE,0,&t))!=ESP_OK) return e;
        t->device_handle=device; t->bEndpointAddress=0x81; t->num_bytes=BULK_SIZE; t->callback=bulk_cb;
        if((e=usb_host_transfer_submit(t))!=ESP_OK) return e;
    }
    atomic_store(&connected,true); return ESP_OK;
}
static void samba_bulk_cb(usb_transfer_t *t)
{
    samba_bulk_status=t->status; samba_bulk_actual=t->actual_num_bytes; samba_bulk_done=true;
}
/* Locate the bulk IN/OUT endpoints of the CDC data interface. */
static esp_err_t samba_find_endpoints(usb_device_handle_t dev,uint8_t *in_ep,uint8_t *out_ep,uint8_t *iface)
{
    const usb_config_desc_t *cfg=NULL;
    esp_err_t e=usb_host_get_active_config_descriptor(dev,&cfg);
    if(e!=ESP_OK || !cfg) return ESP_FAIL;
    uint16_t total=cfg->wTotalLength;
    uint8_t cur_iface=0,found_in=0,found_out=0;
    int offset=0;
    const usb_standard_desc_t *d=(const usb_standard_desc_t *)cfg;
    while((d=usb_parse_next_descriptor(d,total,&offset))!=NULL) {
        if(d->bDescriptorType==USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            cur_iface=((const usb_intf_desc_t *)d)->bInterfaceNumber;
        } else if(d->bDescriptorType==USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
            const usb_ep_desc_t *ep=(const usb_ep_desc_t *)d;
            ESP_LOGI(TAG,"SAM-BA ep addr 0x%02x type %d",ep->bEndpointAddress,USB_EP_DESC_GET_XFERTYPE(ep));
            if(USB_EP_DESC_GET_XFERTYPE(ep)==USB_TRANSFER_TYPE_BULK) {
                uint16_t mps=USB_EP_DESC_GET_MPS(ep);
                if(USB_EP_DESC_GET_EP_DIR(ep)) { if(!found_in){ *in_ep=ep->bEndpointAddress; found_in=1; *iface=cur_iface; samba_bulk_mps=mps; } }
                else { if(!found_out){ *out_ep=ep->bEndpointAddress; found_out=1; *iface=cur_iface; samba_bulk_mps=mps; } }
            }
        }
    }
    return (found_in && found_out) ? ESP_OK : ESP_FAIL;
}
static esp_err_t open_samba(uint8_t addr)
{
    esp_err_t e=usb_host_device_open(client,addr,&samba_device); if(e!=ESP_OK) return e;
    const usb_device_desc_t *d=NULL;
    e=usb_host_get_device_descriptor(samba_device,&d);
    if(!d) { ESP_LOGE(TAG,"SAM-BA open: no device descriptor"); usb_host_device_close(client,samba_device); samba_device=NULL; return ESP_FAIL; }
    ESP_LOGI(TAG,"SAM-BA candidate: VID:PID %04x:%04x",d->idVendor,d->idProduct);
    if(d->idVendor!=AIRSPY_VID || d->idProduct!=AIRSPY_SAMBA_PID) { usb_host_device_close(client,samba_device); samba_device=NULL; return ESP_ERR_NOT_SUPPORTED; }
    uint8_t in_ep=0,out_ep=0,iface=0;
    if(samba_find_endpoints(samba_device,&in_ep,&out_ep,&iface)!=ESP_OK) { ESP_LOGE(TAG,"SAM-BA open: no bulk endpoints"); usb_host_device_close(client,samba_device); samba_device=NULL; return ESP_FAIL; }
    ESP_LOGI(TAG,"SAM-BA bootloader: interface %u, bulk IN 0x%02x, bulk OUT 0x%02x, MPS %u",iface,in_ep,out_ep,samba_bulk_mps);
    e=usb_host_interface_claim(client,samba_device,iface,0); if(e!=ESP_OK) { usb_host_device_close(client,samba_device); samba_device=NULL; return e; }
    samba_bulk_in=in_ep; samba_bulk_out=out_ep;
    /* Activate the CDC data path: set line coding, then assert DTR/RTS. */
    uint8_t coding[7]={0x00,0xc2,0x01,0x00,0x00,0x00,0x08}; /* 115200 8N1 */
    samba_control(0x21,0x20,0,iface,coding,sizeof(coding));
    samba_control(0x21,0x22,3,iface,NULL,0);
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}
bool airspy_samba_connected(void) { return samba_device!=NULL; }
esp_err_t airspy_samba_write(const uint8_t *data,size_t len)
{
    if(!samba_device) return ESP_ERR_INVALID_STATE;
    usb_transfer_t *t=NULL;
    esp_err_t err=usb_host_transfer_alloc(len,0,&t);
    if(err!=ESP_OK) return err;
    memcpy(t->data_buffer,data,len);
    t->device_handle=samba_device; t->bEndpointAddress=samba_bulk_out; t->num_bytes=len; t->callback=samba_bulk_cb;
    samba_bulk_done=false;
    err=usb_host_transfer_submit(t);
    if(err==ESP_OK) {
        int64_t deadline=esp_timer_get_time()+3000000;
        while(!samba_bulk_done && esp_timer_get_time()<deadline) usb_host_client_handle_events(client,pdMS_TO_TICKS(20));
        if(!samba_bulk_done) { ESP_LOGE(TAG,"SAM-BA bulk OUT timeout; restarting"); esp_restart(); }
        else if(samba_bulk_status!=USB_TRANSFER_STATUS_COMPLETED) { ESP_LOGE(TAG,"SAM-BA bulk OUT status %d",samba_bulk_status); err=ESP_FAIL; }
    }
    usb_host_transfer_free(t);
    return err;
}
int airspy_samba_read(uint8_t *data,size_t len,uint32_t timeout_ms)
{
    if(!samba_device) return -1;
    size_t mps = samba_bulk_mps ? samba_bulk_mps : 64;
    size_t alloc_len = ((len + mps - 1) / mps) * mps;
    usb_transfer_t *t=NULL;
    if(usb_host_transfer_alloc(alloc_len,0,&t)!=ESP_OK) return -1;
    t->device_handle=samba_device; t->bEndpointAddress=samba_bulk_in; t->num_bytes=alloc_len; t->callback=samba_bulk_cb;
    samba_bulk_done=false;
    int result=-1;
    if(usb_host_transfer_submit(t)==ESP_OK) {
        int64_t deadline=esp_timer_get_time()+(int64_t)timeout_ms*1000;
        while(!samba_bulk_done && esp_timer_get_time()<deadline) usb_host_client_handle_events(client,pdMS_TO_TICKS(20));
        if(!samba_bulk_done) { ESP_LOGE(TAG,"SAM-BA bulk IN timeout; restarting"); esp_restart(); }
        else if(samba_bulk_status==USB_TRANSFER_STATUS_COMPLETED && samba_bulk_actual>=0 && (size_t)samba_bulk_actual<=len) {
            memcpy(data,t->data_buffer,(size_t)samba_bulk_actual);
            result=samba_bulk_actual;
        } else { ESP_LOGE(TAG,"SAM-BA bulk IN status %d actual %d",samba_bulk_status,samba_bulk_actual); }
    }
    usb_host_transfer_free(t);
    return result;
}
esp_err_t airspy_reboot_to_samba(void)
{
    if(!device) return ESP_ERR_INVALID_STATE;
    /* The receiver's 0x66 handler disables interrupts and spins to a watchdog
     * reset, so the control transfer never completes a status phase — the
     * device simply disconnects. control_transfer_dev waits for that and frees
     * the transfer cleanly; the resulting ESP_FAIL is expected. */
    control(0x40,0x66,0xC0DE,0xBABE,NULL,0);
    ESP_LOGI(TAG,"Reboot-to-SAM-BA requested");
    return ESP_OK;
}
void airspy_power_cycle_and_restart(void)
{
    /* De-power the root port so the receiver's supply rail discharges and it
     * fully resets, then reboot. Startup re-powers the port, completing the
     * receiver's power cycle. */
    usb_host_lib_set_root_port_power(false);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}
void airspy_usb_power_cycle(void)
{
    usb_host_lib_set_root_port_power(false);
    vTaskDelay(pdMS_TO_TICKS(300));
    usb_host_lib_set_root_port_power(true);
}
static void usb_client_task(void *arg)
{
    SemaphoreHandle_t ready=arg;
    usb_host_client_config_t c={.is_synchronous=false,.max_num_event_msg=10,.async={.client_event_callback=usb_event}};
    ESP_ERROR_CHECK(usb_host_client_register(&c,&client)); xSemaphoreGive(ready);
    while(1) {
        usb_host_client_handle_events(client,pdMS_TO_TICKS(10));
        if(new_address && !device && !samba_device) {
            uint8_t addr=new_address; new_address=0;
            esp_err_t e=open_receiver(addr);
            if(e==ESP_ERR_NOT_SUPPORTED) {
                /* Not the normal receiver (03eb:800c); treat it as the SAM-BA
                 * bootloader (03eb:6124) and open it, so a receiver left in
                 * bootloader mode can still be recovered or updated. */
                e=open_samba(addr);
                if(e!=ESP_OK && e!=ESP_ERR_NOT_SUPPORTED) { ESP_LOGE(TAG,"SAM-BA init: %s",esp_err_to_name(e)); esp_restart(); }
            } else if(e!=ESP_OK) { ESP_LOGE(TAG,"Airspy init: %s",esp_err_to_name(e)); esp_restart(); }
        }
        if(device && atomic_load(&connected)) { uint32_t hz=atomic_load(&request_hz); if(hz!=atomic_load(&tuned_hz) && tune(hz)!=ESP_OK) esp_restart(); }
        if(device && atomic_load(&connected)) { uint32_t gain=atomic_load(&requested_gain); if(gain!=atomic_load(&applied_gain) && set_gain(gain)!=ESP_OK)esp_restart(); }
        if(device && atomic_load(&connected)) { uint32_t lna=atomic_load(&requested_lna); if(lna!=atomic_load(&applied_lna) && set_lna(lna)!=ESP_OK)esp_restart(); }
        if(device && atomic_load(&connected)) { uint32_t th=atomic_load(&requested_thresh); if(th!=atomic_load(&applied_thresh) && set_agc_threshold(th)!=ESP_OK)esp_restart(); }
        airspy_update_poll();
    }
}
static void usb_daemon(void *arg)
{
    usb_host_config_t c={.skip_phy_setup=false,.intr_flags=ESP_INTR_FLAG_LEVEL1}; ESP_ERROR_CHECK(usb_host_install(&c));
    if(xTaskCreatePinnedToCore(usb_client_task,"airspy_usb",8192,arg,8,NULL,1)!=pdPASS) esp_restart();
    while(1) { uint32_t flags; usb_host_lib_handle_events(portMAX_DELAY,&flags); }
}
esp_err_t airspy_start(SemaphoreHandle_t ready)
{
    iq_mixer_init(&mixer);
    atomic_store(&dsp_dc,true); atomic_store(&dsp_iq,true); atomic_store(&dsp_reset,false);
#if CONFIG_AIRSPY_DC_REMOVE
    dc_block_init(&dc_block);
#endif
#if CONFIG_AIRSPY_IQ_BALANCE
    iq_balance_init(&balance);
#endif
#if CONFIG_AIRSPY_IQ_BALANCE_FREQ
    iq_balance_freq_init(&balance_freq);
    iq_balance_freq_start_task(&balance_freq, 4);
#endif
#if CONFIG_SPIRAM
    // Only copied I/Q samples go to PSRAM. USB DMA and FreeRTOS control stay internal.
    static StaticStreamBuffer_t control;
    uint8_t *storage = heap_caps_malloc(EXTERNAL_RING_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (storage) {
        iq_ring = xStreamBufferCreateStatic(EXTERNAL_RING_BYTES + 1, 1, storage, &control);
    }
#endif
    if (!iq_ring) iq_ring = xStreamBufferCreate(INTERNAL_RING_BYTES, 1);
    if (!iq_ring) return ESP_ERR_NO_MEM;
    size_t capacity=xStreamBufferSpacesAvailable(iq_ring);
    portENTER_CRITICAL(&input_stats_lock);
    ring_capacity=capacity;
    portEXIT_CRITICAL(&input_stats_lock);
    ESP_LOGI(TAG, "I/Q buffer capacity: %u bytes", (unsigned)ring_capacity);
    atomic_store(&request_hz,7100000);
    return xTaskCreatePinnedToCore(usb_daemon,"usb_daemon",4096,ready,8,NULL,1)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
