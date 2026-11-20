/* Exercise the experimental network command logic using a socketpair and a
 * deterministic receiver; no device or RTOS is required. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../main/spy_server.c"

static uint32_t device_center=7000000, gain_index, dropped_blocks;
static unsigned stream_reads;
static bool inject_overrun, overrun_on_pause;
static bool streaming,fft_enabled,frame_available;
static unsigned fft_stage;
static spy_spectrum_live_frame_t fake_frame;
bool airspy_connected(void){return true;}
uint32_t airspy_dropped_blocks(void){return dropped_blocks;}
uint32_t airspy_sample_rate(void){return 768000;}
uint32_t airspy_frequency(void){return device_center;}
void airspy_request_frequency(uint32_t hz){device_center=hz;}
void airspy_request_gain(uint32_t gain){gain_index=gain;}
uint32_t airspy_gain(void){return gain_index;}
void airspy_set_streaming(bool on)
{
    if(!on && streaming && overrun_on_pause) {
        ++dropped_blocks;
        overrun_on_pause=false;
    }
    streaming=on;
}
size_t airspy_read(void *p,size_t n,TickType_t wait)
{
    static const uint8_t iq[]={0x34,0x12,0xcc,0xff};
    if(!inject_overrun || !streaming || !wait)return 0;
    assert(n>=sizeof(iq));
    if(++stream_reads==2)++dropped_blocks;
    memcpy(p,iq,sizeof(iq));
    return sizeof(iq);
}
void spy_spectrum_live_set_enabled(bool on){fft_enabled=on;}
bool spy_spectrum_live_set_stage(unsigned stage){fft_stage=stage;return stage<=6;}
bool spy_spectrum_live_take(spy_spectrum_live_frame_t *out)
{
    if(!fft_enabled || !frame_available)return false;
    *out=fake_frame;frame_available=false;return true;
}
static void read_exact(int fd,void *data,size_t n)
{
    char *p=data;while(n){ssize_t k=read(fd,p,n);assert(k>0);p+=k;n-=(size_t)k;}
}
static uint32_t receive_message(int fd,uint8_t *body,size_t capacity,uint32_t expected_type,uint32_t expected_stream)
{
    uint8_t h[20];read_exact(fd,h,sizeof(h));
    assert(spy_read_u32(h)==SPY_PROTOCOL_VERSION);
    assert(spy_read_u32(h+4)==expected_type && spy_read_u32(h+8)==expected_stream);
    uint32_t n=spy_read_u32(h+16);assert(n<=capacity);read_exact(fd,body,n);return n;
}
static bool setting(connection_t *c,uint32_t id,uint32_t value)
{
    uint8_t b[8];spy_write_u32(b,id);spy_write_u32(b+4,value);return command(c,2,b,sizeof(b));
}
static void verify_overrun_disconnect(void)
{
    int fd[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    pid_t child=fork();assert(child>=0);
    if(child==0) {
        close(fd[1]);
        inject_overrun=true;
        dropped_blocks=7; /* Historical drops must not reject a new stream. */
        serve_client(fd[0]);
        spy_iq_stats_t stats;spy_server_iq_stats(&stats);
        assert(stats.samples_sent==1 && stats.send_failures==0);
        _exit(0);
    }
    close(fd[0]);
    uint8_t commands[28],body[48];
    spy_write_u32(commands,0);
    spy_write_u32(commands+4,4);
    spy_write_u32(commands+8,SPY_PROTOCOL_VERSION);
    spy_write_u32(commands+12,2);
    spy_write_u32(commands+16,8);
    spy_write_u32(commands+20,1);
    spy_write_u32(commands+24,1);
    assert(write(fd[1],commands,sizeof(commands))==(ssize_t)sizeof(commands));
    assert(receive_message(fd[1],body,sizeof(body),SPY_DEVICE_INFO,0)==48);
    assert(receive_message(fd[1],body,sizeof(body),SPY_CLIENT_SYNC,0)==36);
    assert(receive_message(fd[1],body,sizeof(body),SPY_INT16_IQ,1)==4);
    assert(memcmp(body,(const uint8_t[]){0x34,0x12,0xcc,0xff},4)==0);
    ssize_t remaining=read(fd[1],body,1);
    close(fd[1]);
    int status;assert(waitpid(child,&status,0)==child);
    assert(remaining==0 && WIFEXITED(status) && WEXITSTATUS(status)==0);
    puts("SpyServer overrun: one intact I/Q frame, then a disconnect");
}
static void verify_overrun_during_pause(uint32_t id,uint32_t value)
{
    int fd[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    iq_decimator_t decimator; iq_decimator_reset(&decimator,0);
    uint8_t buffer[BLOCK];
    const uint32_t center=device_center;
    connection_t c={.socket=fd[0],.hello=true,.frequency=center,
                    .decimator=&decimator,.buffer=buffer};
    assert(setting(&c,1,1) && streaming);
    overrun_on_pause=true;
    assert(!setting(&c,id,value));
    assert(!streaming && dropped_blocks>0 && device_center==center);
    close(fd[0]);close(fd[1]);
}
int main(void)
{
    verify_overrun_disconnect();
    verify_overrun_during_pause(101,7074375);
    verify_overrun_during_pause(1,0);
    puts("SpyServer overrun: retune and stop cannot mask a pending drop");
    int fd[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    connection_t c={.socket=fd[0],.digital_gain=1};
    c.buffer=malloc(BLOCK);c.decimator=calloc(1,sizeof(*c.decimator));assert(c.buffer&&c.decimator);
    spy_fft_settings_init(&c.fft,device_center);iq_mixer_init(&c.iq_mixer);
    uint8_t hello[4],body[32768];spy_write_u32(hello,SPY_PROTOCOL_VERSION);
    bool opened=command(&c,0,hello,4);
    if(!opened)fprintf(stderr,"hello failed: socket=%d initialized=%d version=%08x connected=%d errno=%d\n",c.socket,c.hello,spy_read_u32(hello),airspy_connected(),errno);
    assert(opened);
    assert(receive_message(fd[1],body,sizeof(body),SPY_DEVICE_INFO,0)==48);
    assert(spy_read_u32(body+12)==660000);
    assert(receive_message(fd[1],body,sizeof(body),SPY_CLIENT_SYNC,0)==36);
    assert(setting(&c,200,1) && setting(&c,202,3) && fft_stage==3);
    assert(setting(&c,201,7074000));
    assert(setting(&c,205,2048) && setting(&c,203,(uint32_t)-20) && setting(&c,204,120));
    assert(setting(&c,101,7090000));
    assert(receive_message(fd[1],body,sizeof(body),SPY_CLIENT_SYNC,0)==36);
    assert(device_center==7090000 && spy_read_u32(body+8)==7090000);
    assert(setting(&c,0,5) && c.fft_enabled && fft_enabled);
    assert(receive_message(fd[1],body,sizeof(body),SPY_CLIENT_SYNC,0)==36);
    assert(device_center==7074000 && spy_read_u32(body+8)==7074000 && spy_read_u32(body+12)==7090000 && spy_read_u32(body+16)==7074000);
    assert(setting(&c,205,100) && c.fft.pixels==100);
    assert(setting(&c,205,32768) && c.fft.pixels==32768);
    assert(setting(&c,205,2048) && c.fft.pixels==2048);
    assert(setting(&c,1,1) && streaming);
    assert(setting(&c,101,7100000));
    assert(receive_message(fd[1],body,sizeof(body),SPY_CLIENT_SYNC,0)==36);
    assert(device_center==7074000 && spy_read_u32(body+12)==7100000);
    assert(atomic_load(&current_iq_frequency)==7100000);
    fake_frame.center_hz=7074000;fake_frame.stage=3;fake_frame.sample_rate=96000;
    for(unsigned i=0;i<SPY_FFT_SIZE;i++)fake_frame.bins_db[i]=-140;
    fake_frame.bins_db[SPY_FFT_SIZE/2+123]=-50;frame_available=true;
    assert(send_fft(&c));assert(receive_message(fd[1],body,sizeof(body),SPY_UINT8_FFT,4)==2048);
    unsigned peak=0;for(unsigned i=1;i<2048;i++)if(body[i]>body[peak])peak=i;
    assert(body[peak]==191); /* -50 dB in the requested -20 .. -140 dB window */
    double freq=((peak+.5)/2048-.5)*(660000.0/8);
    assert(fabs(freq-123*96000.0/2048)<50);
    /* Changing the requested FFT stage invalidates an old-stage frame. */
    assert(setting(&c,202,2) && fft_stage==2);
    frame_available=true;assert(send_fft(&c));
    assert(recv(fd[1],body,sizeof(body),MSG_DONTWAIT)<0 && (errno==EAGAIN || errno==EWOULDBLOCK));
    fake_frame.stage=2;fake_frame.sample_rate=192000;frame_available=true;
    assert(setting(&c,203,0) && setting(&c,204,100));
    assert(send_fft(&c));assert(receive_message(fd[1],body,sizeof(body),SPY_UINT8_FFT,4)==2048);
    peak=0;for(unsigned i=1;i<2048;i++)if(body[i]>body[peak])peak=i;
    assert(body[peak]==128); /* -50 dB in a 0 .. -100 dB window */
    assert(!setting(&c,203,101));assert(c.fft.db_offset==0);
    assert(!setting(&c,202,7));assert(c.fft.stage==2);
    assert(setting(&c,201,7080000));
    assert(receive_message(fd[1],body,sizeof(body),SPY_CLIENT_SYNC,0)==36);
    assert(device_center==7080000 && spy_read_u32(body+12)==7100000);
    assert(setting(&c,0,1));
    assert(receive_message(fd[1],body,sizeof(body),SPY_CLIENT_SYNC,0)==36);
    assert(!fft_enabled && device_center==7100000);
    free(c.buffer);free(c.decimator);free(c.fft_frame);free(c.fft_output);close(fd[0]);close(fd[1]);
    puts("Experimental SpyServer: settings, independent tuning, sync and FFT frame passed");
}
