#include "spy_server.h"
#include "spy_protocol.h"
#include "airspy.h"
#include "iq_decimator.h"
#if CONFIG_AIRSPY_FFT_SERVER
#include "iq_mixer.h"
#include "spy_fft_settings.h"
#include "spy_spectrum_live.h"
#include "spy_fft.h"
#endif
#include <errno.h>
#include <math.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PORT 5555
#define BLOCK 16384
/* Only advertise rates that meet sustained hardware throughput. */
#define SUPPORTED_DECIMATION_STAGES IQ_DECIMATOR_MAX_STAGES
static const char *TAG="spy_server";
static atomic_uint connections;
static uint64_t bytes_sent;
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool client_connected, server_listening;
static atomic_uint current_stage;
static atomic_uint current_iq_frequency;
static uint64_t clipped_components;
static spy_iq_stats_t iq_stats;
void spy_server_iq_stats(spy_iq_stats_t *stats)
{
    portENTER_CRITICAL(&stats_lock);*stats=iq_stats;portEXIT_CRITICAL(&stats_lock);
}
uint64_t spy_server_clipped_components(void) { portENTER_CRITICAL(&stats_lock);uint64_t value=clipped_components;portEXIT_CRITICAL(&stats_lock);return value; }
uint32_t spy_server_iq_sample_rate(void) { return atomic_load(&client_connected)?airspy_sample_rate()>>atomic_load(&current_stage):0; }
uint32_t spy_server_iq_frequency(void) { return atomic_load(&client_connected)?atomic_load(&current_iq_frequency):0; }
uint64_t spy_server_bytes_sent(void) { portENTER_CRITICAL(&stats_lock); uint64_t value=bytes_sent; portEXIT_CRITICAL(&stats_lock); return value; }
uint32_t spy_server_connections(void) { return atomic_load(&connections); }
bool spy_server_client_connected(void) { return atomic_load(&client_connected); }
bool spy_server_listening(void) { return atomic_load(&server_listening); }
typedef struct { int socket; bool hello, streaming; uint32_t control_sequence,iq_sequence; uint32_t frequency; unsigned digital_gain_db; float digital_gain; iq_decimator_t *decimator; uint8_t *buffer;
#if CONFIG_AIRSPY_FFT_SERVER
    bool fft_enabled;uint32_t fft_sequence;spy_fft_settings_t fft;iq_mixer_t iq_mixer;
    spy_spectrum_live_frame_t *fft_frame;uint8_t *fft_output;
#endif
    uint32_t iq_drops_at_start;
} connection_t;

static void start_iq(connection_t *c)
{
    c->iq_drops_at_start=airspy_dropped_blocks();
    airspy_set_streaming(true);
}
static bool iq_overrun(const connection_t *c)
{
    if(airspy_dropped_blocks()==c->iq_drops_at_start)return false;
    ESP_LOGW(TAG,"I/Q input overrun; closing discontinuous stream");
    return true;
}
static void discard_pending(connection_t *c)
{
    while(airspy_read(c->buffer,BLOCK,0)>0){}
    iq_decimator_reset(c->decimator,c->decimator->stages);
}
static bool send_all(int s,const void *data,size_t len)
{
    const uint8_t *p=data;
    while(len) { int n=send(s,p,len,0); if(n<=0) { if(n<0&&errno==EINTR)continue; return false; } p+=n;len-=n;portENTER_CRITICAL(&stats_lock); bytes_sent+=(unsigned)n; portEXIT_CRITICAL(&stats_lock); }
    return true;
}
static bool message(connection_t *c,uint32_t type,uint32_t stream,const void *body,size_t len)
{
    uint8_t header[20];spy_make_header(header,type,stream,c->control_sequence++,(uint32_t)len);
    return send_all(c->socket,header,sizeof(header)) && (!len || send_all(c->socket,body,len));
}
static bool send_sync(connection_t *c)
{
    uint8_t sync[36];spy_make_client_sync(sync,c->frequency);spy_write_u32(sync+4,airspy_gain());
#if CONFIG_AIRSPY_FFT_SERVER
    spy_write_u32(sync+8,airspy_frequency());spy_write_u32(sync+16,c->fft_enabled?c->fft.center_hz:airspy_frequency());
#endif
    return message(c,SPY_CLIENT_SYNC,0,sync,sizeof(sync));
}
static bool retune(connection_t *c,uint32_t hz)
{
    if(hz==airspy_frequency())return true;
    bool resume=c->streaming;airspy_set_streaming(false);
    /* Check after the producer stops so a retune cannot hide a pending drop. */
    if(resume && iq_overrun(c))return false;
    discard_pending(c);
    airspy_request_frequency(hz);
    int64_t deadline=esp_timer_get_time()+3000000;
    while(airspy_connected() && airspy_frequency()!=hz && esp_timer_get_time()<deadline)vTaskDelay(1);
    if(!airspy_connected() || airspy_frequency()!=hz)return false;
    vTaskDelay(pdMS_TO_TICKS(50));
    if(resume)start_iq(c);
    return true;
}
#if CONFIG_AIRSPY_FFT_SERVER
static void set_iq_offset(connection_t *c)
{
    iq_mixer_set(&c->iq_mixer,(double)c->frequency-(double)airspy_frequency(),airspy_sample_rate());
}
static bool send_fft(connection_t *c)
{
    if(!c->fft_enabled || !spy_spectrum_live_take(c->fft_frame))return true;
    spy_spectrum_live_frame_t *f=c->fft_frame;
    if(f->center_hz!=c->fft.center_hz || f->stage!=c->fft.stage)return true;
    float span=(float)(660000u>>c->fft.stage);
    if(!spy_fft_render_db(f->bins_db,c->fft_output+20,c->fft.pixels,(float)f->sample_rate,0,span,(float)c->fft.db_offset,(float)c->fft.db_range))return false;
    spy_make_header(c->fft_output,SPY_UINT8_FFT,4,c->fft_sequence++,c->fft.pixels);
    return send_all(c->socket,c->fft_output,20+c->fft.pixels);
}
#endif
static bool command(void *arg,uint32_t cmd,const uint8_t *body,uint32_t len)
{
    connection_t *c=arg;
    if(cmd==0) {
        if(c->hello || len<4 || (spy_read_u32(body)>>24)!=2 || !airspy_connected())return false;
        c->hello=true;c->frequency=airspy_frequency();atomic_store(&current_iq_frequency,c->frequency);
        uint8_t info[48];spy_make_device_info(info,airspy_sample_rate(),
#if CONFIG_AIRSPY_FFT_SERVER
            660000,
#else
            0,
#endif
            0,SUPPORTED_DECIMATION_STAGES);
        spy_write_u32(info+20,1);spy_write_u32(info+24,AIRSPY_GAIN_MAX);
        return message(c,SPY_DEVICE_INFO,0,info,sizeof(info)) && send_sync(c);
    }
    if(!c->hello)return false;
    if(cmd==3)return message(c,SPY_PONG,0,NULL,0);
    if(cmd!=2 || len<8)return false;
    uint32_t setting=spy_read_u32(body),value=spy_read_u32(body+4);
    switch(setting) {
    case 0:
#if CONFIG_AIRSPY_FFT_SERVER
        if(value!=1 && value!=5)return false;
        if(value==5 && !c->fft_frame) {
            c->fft_frame=malloc(sizeof(*c->fft_frame));c->fft_output=malloc(20+c->fft.pixels);
            if(!c->fft_frame || !c->fft_output)return false;
        }
        c->fft_enabled=value==5;
        spy_spectrum_live_set_enabled(c->fft_enabled);
        if(!retune(c,c->fft_enabled?c->fft.center_hz:c->frequency))return false;
        set_iq_offset(c);
        return send_sync(c);
#endif
        break;
    case 1:
        if(value>1)return false;
        if(!value) {
            airspy_set_streaming(false);
            if(c->streaming && iq_overrun(c))return false;
            c->streaming=false;discard_pending(c);
        }
        else if(!c->streaming) { discard_pending(c);c->streaming=true;start_iq(c); }
        break;
    case 2: if(value>AIRSPY_GAIN_MAX)return false;airspy_request_gain(value);break;
    case 100: if(value!=2)return false; break; // only signed 16 bit is advertised
    case 101: // IQ center; the Airspy LO has kHz steps
        if(value<500 || value>260000000)return false;
#if CONFIG_AIRSPY_FFT_SERVER
        if(c->fft_enabled) { if(value!=c->frequency)discard_pending(c); }
        else
#endif
        if(!retune(c,value))return false;
        c->frequency=value;atomic_store(&current_iq_frequency,value);
#if CONFIG_AIRSPY_FFT_SERVER
        set_iq_offset(c);
#endif
        return send_sync(c);
    case 102:
        if(value>SUPPORTED_DECIMATION_STAGES)return false;
        iq_decimator_reset(c->decimator,value);atomic_store(&current_stage,value);break;
    case 103: if(value>60)return false; c->digital_gain_db=value;c->digital_gain=powf(10.f,(float)value/20.f);break;
    case 200: case 201: case 202: case 203: case 204: case 205:
#if CONFIG_AIRSPY_FFT_SERVER
        {
        spy_fft_settings_t next=c->fft;
        if(spy_fft_settings_apply(&next,setting,value,SUPPORTED_DECIMATION_STAGES)!=SPY_FFT_SETTING_OK)return false;
        if(setting==205 && c->fft_output && value!=c->fft.pixels) {
            uint8_t *resized=realloc(c->fft_output,20+value);if(!resized)return false;c->fft_output=resized;
        }
        c->fft=next;
        if(setting==202 && !spy_spectrum_live_set_stage(value))return false;
        if(setting==201 && c->fft_enabled) { if(!retune(c,value))return false;set_iq_offset(c);return send_sync(c); }
        }
#endif
        break;
    default: ESP_LOGW(TAG,"Ignoring setting %" PRIu32,setting); break;
    }
    return true;
}
static void serve_client(int sock)
{
    connection_t c={.socket=sock,.digital_gain=1.f}; spy_parser_t *parser=calloc(1,sizeof(*parser)); uint8_t *iq=malloc(BLOCK+20);
    c.decimator=calloc(1,sizeof(*c.decimator));
    if(!parser || !iq || !c.decimator)goto done;
    c.buffer=iq;
#if CONFIG_AIRSPY_FFT_SERVER
    spy_fft_settings_init(&c.fft,airspy_frequency());iq_mixer_init(&c.iq_mixer);
#endif
    /* Bound a stalled peer to ~1 s of drops before the send() times out and the
     * client is disconnected; the USB ring is not drained while send() blocks. */
    struct timeval timeout={.tv_sec=1,.tv_usec=0}; setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    int keepalive=1, idle=30, interval=10, probes=3, nodelay=1;
    setsockopt(sock,SOL_SOCKET,SO_KEEPALIVE,&keepalive,sizeof(keepalive));
    setsockopt(sock,IPPROTO_TCP,TCP_KEEPIDLE,&idle,sizeof(idle));
    setsockopt(sock,IPPROTO_TCP,TCP_KEEPINTVL,&interval,sizeof(interval));
    setsockopt(sock,IPPROTO_TCP,TCP_KEEPCNT,&probes,sizeof(probes));
    /* Disable Nagle: each I/Q block and FFT frame is sent as header+body, and
     * small control/sync messages must not be coalesced into the stream. */
    setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,&nodelay,sizeof(nodelay));
    int64_t hello_deadline=esp_timer_get_time()+10000000;
    spy_parser_init(parser);
    atomic_store(&current_stage,0);atomic_store(&current_iq_frequency,0);atomic_store(&client_connected,true); atomic_fetch_add(&connections,1);
    ESP_LOGI(TAG,"Client connected");
    while(airspy_connected()) {
        if(!c.hello && esp_timer_get_time()>hello_deadline) { ESP_LOGW(TAG,"Client handshake timed out"); break; }
        fd_set set;FD_ZERO(&set);FD_SET(sock,&set);struct timeval wait={.tv_sec=0,.tv_usec=c.streaming?0:10000};
        int n=select(sock+1,&set,NULL,NULL,&wait);
        if(n<0) { if(errno==EINTR)continue; break; }
        if(n>0) { uint8_t buf[1024];int got=recv(sock,buf,sizeof(buf),0);if(got<=0 || !spy_parser_feed(parser,buf,got,command,&c)) break; }
        if(c.streaming) { size_t got=airspy_read(iq+20,BLOCK,pdMS_TO_TICKS(10));
            /* Lost input may straddle this read. End the stream rather than
             * deliver a gap as contiguous samples with consecutive headers. */
            if(iq_overrun(&c))break;
            if(got) {
                uint32_t before=c.decimator->clipped_components;
#if CONFIG_AIRSPY_FFT_SERVER
                if(c.fft_enabled && c.frequency!=airspy_frequency())
                    got=4*iq_decimator_process_mixed(c.decimator,&c.iq_mixer,(int16_t *)(iq+20),got/4,c.digital_gain);
                else
#endif
                got=4*iq_decimator_process(c.decimator,(int16_t *)(iq+20),got/4,c.digital_gain);
                uint32_t clipped=c.decimator->clipped_components-before;if(clipped) { portENTER_CRITICAL(&stats_lock);clipped_components+=clipped;portEXIT_CRITICAL(&stats_lock); }
                if(got) {
                    spy_make_header(iq,SPY_INT16_IQ|(c.digital_gain_db<<16),1,c.iq_sequence++,(uint32_t)got);
                    int64_t started=esp_timer_get_time();
                    bool sent=send_all(c.socket,iq,got+20);
                    int64_t elapsed=esp_timer_get_time()-started;
                    uint32_t send_us=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
                    portENTER_CRITICAL(&stats_lock);
                    if(sent)iq_stats.samples_sent+=got/4;
                    else iq_stats.send_failures++;
                    if(send_us>iq_stats.max_send_us)iq_stats.max_send_us=send_us;
                    portEXIT_CRITICAL(&stats_lock);
                    if(!sent)break;
                }
            } else {
                portENTER_CRITICAL(&stats_lock);iq_stats.empty_reads++;portEXIT_CRITICAL(&stats_lock);
            }
#if CONFIG_AIRSPY_FFT_SERVER
            if(!send_fft(&c))break;
#endif
        }
    }
    ESP_LOGI(TAG,"Client disconnected");
done:
    atomic_store(&client_connected,false);
    airspy_set_streaming(false);
#if CONFIG_AIRSPY_FFT_SERVER
    spy_spectrum_live_set_enabled(false);free(c.fft_frame);free(c.fft_output);
#endif
    if(iq) { while(airspy_read(iq,BLOCK,0)>0){} }
    free(c.decimator);free(iq);free(parser);close(sock);
}
static void server_task(void *arg)
{
    (void)arg;
    for(;;) {
        int listener=socket(AF_INET,SOCK_STREAM,IPPROTO_IP); if(listener<0) { vTaskDelay(pdMS_TO_TICKS(2000));continue; }
        int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
        struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(PORT),.sin_addr.s_addr=htonl(INADDR_ANY)};
        if(bind(listener,(struct sockaddr *)&addr,sizeof(addr))<0 || listen(listener,1)<0) { close(listener);vTaskDelay(pdMS_TO_TICKS(2000));continue; }
        atomic_store(&server_listening,true);
        ESP_LOGI(TAG,"Listening on TCP %d",PORT);
        for(;;) { int s=accept(listener,NULL,NULL);if(s<0)break;serve_client(s); }
        atomic_store(&server_listening,false);
        close(listener);
    }
}
esp_err_t spy_server_start(void) { return xTaskCreatePinnedToCore(server_task,"spy_server",6144,NULL,5,NULL,0)==pdPASS?ESP_OK:ESP_ERR_NO_MEM; }
