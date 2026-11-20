#include "spy_protocol.h"
#include <string.h>
uint32_t spy_read_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
void spy_write_u32(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
void spy_parser_init(spy_parser_t *p) { memset(p,0,sizeof(*p)); }
bool spy_parser_feed(spy_parser_t *p, const uint8_t *src, size_t n, spy_command_cb cb, void *ctx)
{
    while(n) {
        if (p->have < 8) {
            size_t take=8-p->have; if(take>n)take=n;
            memcpy(p->header+p->have,src,take); p->have+=take; src+=take; n-=take;
            if(p->have<8) continue;
            p->type=spy_read_u32(p->header); p->length=spy_read_u32(p->header+4);
            if(p->length>SPY_MAX_COMMAND_BODY) return false;
            if(!p->length) { if(!cb(ctx,p->type,p->body,0)) return false; p->have=0; }
        } else {
            size_t have=p->have-8, take=p->length-have; if(take>n)take=n;
            memcpy(p->body+have,src,take); p->have+=take; src+=take; n-=take;
            if(p->have==8+p->length) { if(!cb(ctx,p->type,p->body,p->length)) return false; p->have=0; }
        }
    }
    return true;
}
void spy_make_header(uint8_t out[20], uint32_t type, uint32_t stream, uint32_t sequence, uint32_t length)
{
    spy_write_u32(out,SPY_PROTOCOL_VERSION); spy_write_u32(out+4,type); spy_write_u32(out+8,stream); spy_write_u32(out+12,sequence); spy_write_u32(out+16,length);
}
void spy_make_device_info(uint8_t out[48], uint32_t rate, uint32_t bandwidth, uint32_t serial, uint32_t stages)
{
    uint32_t words[12]={2,serial,rate,bandwidth,stages,0,0,500,260000000,16,0,2};
    for(size_t i=0;i<12;i++) spy_write_u32(out+4*i,words[i]);
}
void spy_make_client_sync(uint8_t out[36], uint32_t hz)
{
    uint32_t words[9]={1,0,hz,hz,hz,0,0,0,0};
    for(size_t i=0;i<9;i++) spy_write_u32(out+4*i,words[i]);
}
