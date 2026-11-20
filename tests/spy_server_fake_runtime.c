/* Loopback integration server for SDROxide's unmodified client. The radio and
 * FFT worker are simulated; protocol parsing and IQ mixing are real code. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../main/spy_server.c"
static uint32_t center=7000000,gain_value,stage;
static bool is_streaming,fft_on;
static uint64_t samples;
static int64_t last_fft;
static int64_t now_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
bool airspy_connected(void){return true;}
uint32_t airspy_dropped_blocks(void){return 0;}
uint32_t airspy_sample_rate(void){return 768000;}
uint32_t airspy_frequency(void){return center;}
void airspy_request_frequency(uint32_t hz){center=hz;}
void airspy_request_gain(uint32_t gain){gain_value=gain;}
uint32_t airspy_gain(void){return gain_value;}
void airspy_set_streaming(bool on){is_streaming=on;}
size_t airspy_read(void *p,size_t n,TickType_t wait)
{
    if(!is_streaming || !wait)return 0;
    struct timespec t={.tv_sec=0,.tv_nsec=5333333};nanosleep(&t,NULL);
    size_t count=n/4;int16_t *iq=p;
    double freq=7125000.0-center;
    for(size_t i=0;i<count;i++) {
        double a=6.2831853071795864769*freq*(samples+i)/768000.0;
        iq[2*i]=(int16_t)lround(8000*cos(a));iq[2*i+1]=(int16_t)lround(8000*sin(a));
    }
    samples+=count;return count*4;
}
void spy_spectrum_live_set_enabled(bool enabled){fft_on=enabled;}
bool spy_spectrum_live_set_stage(unsigned s){stage=s;return s<=6;}
bool spy_spectrum_live_take(spy_spectrum_live_frame_t *out)
{
    if(!fft_on || now_us()-last_fft<100000)return false;
    last_fft=now_us();memset(out,0,sizeof(*out));out->center_hz=center;out->stage=stage;out->sample_rate=768000>>stage;
    for(unsigned i=0;i<SPY_FFT_SIZE;i++)out->bins_db[i]=-100;
    return true;
}
int main(void)
{
    int listener=socket(AF_INET,SOCK_STREAM,0);if(listener<0)return 1;
    int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(15555),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    if(bind(listener,(struct sockaddr *)&addr,sizeof(addr))<0 || listen(listener,1)<0)return 2;
    puts("loopback SpyServer ready");fflush(stdout);
    int client=accept(listener,NULL,NULL);if(client<0)return 3;
    serve_client(client);close(listener);return 0;
}
