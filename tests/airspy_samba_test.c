#include "airspy_samba.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fake SAM-BA transport with a small memory model of the target: 64 KiB flash
 * (0x80000..0x90000), a 4 KiB SRAM window (0x20001000..0x20001fff), and the
 * SAM3U chip-id/flash-status registers. It parses the text commands, records
 * every written byte, and simulates the word-copy applet on "G". */

#define LOG_CAP (1 << 18)
#define FLASH_SIZE (256u * 256u)
#define SRAM_BASE 0x20001000u
#define SRAM_SIZE 0x1000u

#define CHIPID 0x400e0740u
#define EEFC_FSR 0x400e0808u
#define APP_SLOT_STACK (SRAM_BASE + 0x20u)

typedef struct {
    uint8_t log[LOG_CAP];
    size_t log_len;

    uint8_t flash[FLASH_SIZE];
    uint8_t sram[SRAM_SIZE];

    /* Pending "S" data phase. */
    int expect_data;
    uint32_t s_addr;

    uint8_t resp[256];
    size_t resp_len;
    size_t resp_off;

    int writes;
} fake_t;

static fake_t *g_fake;

static uint32_t le32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

static uint8_t *mem_addr(fake_t *f, uint32_t addr, size_t len)
{
    if (addr >= SRAM_BASE && addr + len <= SRAM_BASE + SRAM_SIZE)
        return f->sram + (addr - SRAM_BASE);
    if (addr >= AIRSPY_SAMBA_FLASH_BASE && addr + len <= AIRSPY_SAMBA_FLASH_BASE + FLASH_SIZE)
        return f->flash + (addr - AIRSPY_SAMBA_FLASH_BASE);
    return NULL;
}

static void set_resp(fake_t *f, const void *data, size_t len)
{
    assert(len <= sizeof(f->resp));
    memcpy(f->resp, data, len);
    f->resp_len = len;
    f->resp_off = 0;
}

static void applet_copy(fake_t *f)
{
    uint8_t *dst = mem_addr(f, le32_at(f->sram + 0x28), 4);
    uint8_t *src = mem_addr(f, le32_at(f->sram + 0x2c), 4);
    uint32_t words = le32_at(f->sram + 0x30);
    for (uint32_t i = 0; i < words; i++) {
        uint8_t *d = mem_addr(f, le32_at(f->sram + 0x28) + i * 4, 4);
        uint8_t *s = mem_addr(f, le32_at(f->sram + 0x2c) + i * 4, 4);
        (void)dst; (void)src;
        if (d && s) {
            uint32_t v = le32_at(s);
            put_le32(d, v);
        }
    }
}

static int fake_write(const uint8_t *data, size_t len)
{
    fake_t *f = g_fake;
    assert(f->log_len + len <= LOG_CAP);
    memcpy(f->log + f->log_len, data, len);
    f->log_len += len;
    f->writes++;

    /* Data phase of a previous "S" command. */
    if (f->expect_data > 0) {
        uint8_t *p = mem_addr(f, f->s_addr, len);
        if (p)
            memcpy(p, data, len);
        f->expect_data = 0;
        return 0;
    }

    char cmd[32];
    if (len >= 2 && len < sizeof(cmd)) {
        memcpy(cmd, data, len);
        cmd[len] = 0;
        uint32_t a = 0, b = 0;
        switch (cmd[0]) {
        case 'N': {
            static const uint8_t r[] = {0x0d, 0x0a};
            set_resp(f, r, 2);
            break;
        }
        case 'V': {
            static const uint8_t r[] = "v1.1 Dec 15 2011 19:25:04";
            set_resp(f, r, sizeof(r) - 1);
            break;
        }
        case 'W':
            if (sscanf(cmd, "W%X,%X", &a, &b) == 2) {
                uint8_t *p = mem_addr(f, a, 4);
                if (p)
                    put_le32(p, b);
            }
            break;
        case 'w': {
            if (sscanf(cmd, "w%X,4", &a) == 1) {
                uint8_t r[4];
                uint32_t v = 0;
                if (a == CHIPID)
                    v = 0x28090560u;
                else if (a == EEFC_FSR)
                    v = 0x00000001u;
                else {
                    uint8_t *p = mem_addr(f, a, 4);
                    if (p)
                        v = le32_at(p);
                }
                put_le32(r, v);
                set_resp(f, r, 4);
            }
            break;
        }
        case 'o': {
            if (sscanf(cmd, "o%X,4", &a) == 1) {
                uint8_t b = 0xff;
                uint8_t *p = mem_addr(f, a, 1);
                if (p)
                    b = *p;
                set_resp(f, &b, 1);
            }
            break;
        }
        case 'R':
            if (sscanf(cmd, "R%X,%X", &a, &b) == 2) {
                uint8_t r[256];
                assert(b <= sizeof(r));
                uint8_t *p = mem_addr(f, a, b);
                if (p)
                    memcpy(r, p, b);
                else
                    memset(r, 0xff, b);
                set_resp(f, r, b);
            }
            break;
        case 'S':
            if (sscanf(cmd, "S%X,%X", &a, &b) == 2) {
                f->s_addr = a;
                f->expect_data = (int)b;
            }
            break;
        case 'G':
            if (sscanf(cmd, "G%X", &a) == 1 && a == APP_SLOT_STACK)
                applet_copy(f);
            break;
        default:
            break;
        }
    }
    return 0;
}

static int fake_read(uint8_t *data, size_t len, uint32_t timeout_ms)
{
    fake_t *f = g_fake;
    (void)timeout_ms;
    size_t avail = f->resp_len - f->resp_off;
    if (avail == 0)
        return 0;
    if (len > avail)
        len = avail;
    memcpy(data, f->resp + f->resp_off, len);
    f->resp_off += len;
    return (int)len;
}

static void fake_delay(uint32_t ms)
{
    (void)ms;
}

static int find_seq(const uint8_t *hay, size_t hay_len, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0 || n > hay_len)
        return 0;
    for (size_t i = 0; i + n <= hay_len; i++)
        if (memcmp(hay + i, needle, n) == 0)
            return 1;
    return 0;
}

int main(void)
{
    fake_t f;
    memset(&f, 0, sizeof(f));
    g_fake = &f;

    airspy_samba_transport_t t = {
        .write = fake_write,
        .read = fake_read,
        .delay_ms = fake_delay,
    };

    airspy_samba_t *s = airspy_samba_create(&t);
    assert(s);
    assert(airspy_samba_create(NULL) == NULL);

    char version[64] = {0};
    assert(airspy_samba_connect(s, version) == AIRSPY_SAMBA_OK);
    assert(strcmp(version, "v1.1 Dec 15 2011 19:25:04") == 0);
    assert(airspy_samba_chip_id(s) == 0x28090560u);
    assert(airspy_samba_is_sam3u1(s));

    /* A 600-byte image spans three pages (256 + 256 + 88). */
    uint8_t image[600];
    for (size_t i = 0; i < sizeof(image); i++)
        image[i] = (uint8_t)(i * 7 + 1);

    uint32_t last_done = 0, last_total = 0, calls = 0;
    airspy_samba_progress_t progress = NULL;
    (void)progress;
    airspy_samba_err_t e = airspy_samba_flash_image(s, image, sizeof(image), NULL, NULL);
    assert(e == AIRSPY_SAMBA_OK);

    /* The image must now be present in the fake flash (verify passed). */
    assert(memcmp(f.flash, image, sizeof(image)) == 0);
    /* The padded tail of the last page stays 0xFF. */
    assert(f.flash[600] == 0xff && f.flash[767] == 0xff);

    /* Exact command framing. */
    assert(find_seq(f.log, f.log_len, "N#"));
    assert(find_seq(f.log, f.log_len, "V#"));
    assert(find_seq(f.log, f.log_len, "w400E0740,4#"));
    assert(find_seq(f.log, f.log_len, "W400E0800,00000600#"));
    assert(find_seq(f.log, f.log_len, "S20001000,00000034#"));
    assert(find_seq(f.log, f.log_len, "W20001020,20002000#"));
    assert(find_seq(f.log, f.log_len, "W20001030,00000040#"));
    assert(find_seq(f.log, f.log_len, "W400E0804,5A000005#")); /* erase all */
    assert(find_seq(f.log, f.log_len, "S20001034,00000100#"));
    assert(find_seq(f.log, f.log_len, "W20001028,00080000#")); /* dst page 0 */
    assert(find_seq(f.log, f.log_len, "W2000102C,20001034#")); /* src */
    assert(find_seq(f.log, f.log_len, "W20001024,20001001#")); /* reset slot */
    assert(find_seq(f.log, f.log_len, "G20001020#"));          /* runv */
    assert(find_seq(f.log, f.log_len, "W400E0804,5A000003#")); /* EWP page 0 */
    assert(find_seq(f.log, f.log_len, "W400E0804,5A00010B#")); /* set GPNVM1 */

    /* The watchdog reset is a separate step owned by the update orchestrator. */
    assert(airspy_samba_reset(s) == AIRSPY_SAMBA_OK);
    assert(find_seq(f.log, f.log_len, "W400E1200,A500000D#")); /* watchdog reset */

    /* The word-copy applet must be loaded verbatim (BOSSA WordCopyArm). */
    static const uint8_t applet[52] = {
        0x09, 0x48, 0x0a, 0x49, 0x0a, 0x4a, 0x02, 0xe0, 0x08, 0xc9, 0x08, 0xc0, 0x01, 0x3a, 0x00, 0x2a,
        0xfa, 0xd1, 0x04, 0x48, 0x00, 0x28, 0x01, 0xd1, 0x01, 0x48, 0x85, 0x46, 0x70, 0x47, 0xc0, 0x46,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    int found = 0;
    for (size_t i = 0; i + sizeof(applet) <= f.log_len; i++)
        if (memcmp(f.log + i, applet, sizeof(applet)) == 0)
            found = 1;
    assert(found && "word-copy applet bytes not found in transport log");

    (void)last_done;
    (void)last_total;
    (void)calls;
    airspy_samba_destroy(s);
    puts("SAM-BA protocol: connect, chip id, erase/write/verify/boot framing passed");
    return 0;
}
