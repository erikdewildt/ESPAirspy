#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define SPY_PROTOCOL_VERSION 0x020006a4u
#define SPY_MAX_COMMAND_BODY 4096u
#define SPY_DEVICE_INFO 0u
#define SPY_CLIENT_SYNC 1u
#define SPY_PONG 2u
#define SPY_INT16_IQ 101u
#define SPY_UINT8_FFT 301u

typedef struct {
    uint8_t header[8];
    uint8_t body[SPY_MAX_COMMAND_BODY];
    size_t have;
    uint32_t type;
    uint32_t length;
} spy_parser_t;
typedef bool (*spy_command_cb)(void *context, uint32_t command, const uint8_t *body, uint32_t length);
void spy_parser_init(spy_parser_t *parser);
bool spy_parser_feed(spy_parser_t *parser, const uint8_t *data, size_t length, spy_command_cb callback, void *context);
uint32_t spy_read_u32(const uint8_t *p);
void spy_write_u32(uint8_t *p, uint32_t value);
void spy_make_header(uint8_t out[20], uint32_t type, uint32_t stream, uint32_t sequence, uint32_t length);
void spy_make_device_info(uint8_t out[48], uint32_t sample_rate, uint32_t bandwidth, uint32_t serial, uint32_t decimation_stages);
void spy_make_client_sync(uint8_t out[36], uint32_t frequency);
