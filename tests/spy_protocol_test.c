#include "spy_protocol.h"
#include <assert.h>
#include <stdio.h>
static int calls;
static bool on_command(void *unused, uint32_t command,const uint8_t *body,uint32_t len) {
    (void)unused; calls++; if(calls==1) { assert(command==2 && len==8); assert(spy_read_u32(body)==101); assert(spy_read_u32(body+4)==7100000); } else { assert(command==3 && len==0); } return true;
}
int main(void) {
    spy_parser_t p; spy_parser_init(&p); uint8_t in[24];
    spy_write_u32(in,2); spy_write_u32(in+4,8); spy_write_u32(in+8,101); spy_write_u32(in+12,7100000); spy_write_u32(in+16,3); spy_write_u32(in+20,0);
    for(unsigned i=0;i<sizeof(in);i++) { assert(spy_parser_feed(&p,in+i,1,on_command,NULL)); }
    assert(calls==2);
    uint8_t big[8];spy_write_u32(big,2);spy_write_u32(big+4,SPY_MAX_COMMAND_BODY+1);assert(!spy_parser_feed(&p,big,sizeof(big),on_command,NULL));
    uint8_t info[48],hdr[20];spy_make_device_info(info,768000,660000,42,6);assert(spy_read_u32(info+16)==6);assert(spy_read_u32(info)==2 && spy_read_u32(info+8)==768000 && spy_read_u32(info+44)==2);
    spy_make_header(hdr,101,1,7,16384);assert(spy_read_u32(hdr)==SPY_PROTOCOL_VERSION && spy_read_u32(hdr+16)==16384); puts("SpyServer protocol tests passed");
}
