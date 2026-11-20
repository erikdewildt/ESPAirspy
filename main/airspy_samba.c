#include "airspy_samba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* EEFC0 register block for the SAM3U (single-plane SAM3U1). */
#define EEFC_FMR 0x400e0800u
#define EEFC_FCR 0x400e0804u
#define EEFC_FSR 0x400e0808u
#define EEFC_FRR 0x400e080cu

/* EEFC command encoding: FCR = (0x5A << 24) | (arg << 8) | cmd. */
#define EEFC_KEY 0x5au
#define FCR(cmd, arg) ((uint32_t)((EEFC_KEY << 24) | ((uint32_t)(arg) << 8) | (cmd)))
#define FCMD_EWP 0x3u
#define FCMD_EA  0x5u
#define FCMD_SGPB 0xbu
#define FCMD_GGPB 0xdu

#define FSR_FRDY 0x1u
#define FSR_CMDE 0x2u
#define FSR_FLOCKE 0x4u
#define FSR_FLERR 0x8u

/* SAM3U errata: flash wait states must be 6. */
#define EEFC_FMR_FWS6 0x600u

/* WDT_MR for a watchdog-triggered reset back to the application. */
#define SAM3U_WDT_MR 0x400e1200u
#define SAM3U_WDT_RESET 0xa500000du

/* CHIPID_CIDR register for the SAM3U family. */
#define SAM3U_CHIPID 0x400e0740u

/* Word-copy applet (52 bytes), identical to BOSSA's WordCopyArm. The first 32
 * bytes are Thumb-2 code; the trailing 20 bytes are the data slots
 * (stack@0x20, reset@0x24, dst@0x28, src@0x2c, words@0x30) overwritten at
 * runtime. The code copies `words` 32-bit words from src to dst with
 * LDMIA/STMIA, then returns to the monitor. */
static const uint8_t k_applet[52] = {
    0x09, 0x48, 0x0a, 0x49, 0x0a, 0x4a, 0x02, 0xe0, 0x08, 0xc9, 0x08, 0xc0, 0x01, 0x3a, 0x00, 0x2a,
    0xfa, 0xd1, 0x04, 0x48, 0x00, 0x28, 0x01, 0xd1, 0x01, 0x48, 0x85, 0x46, 0x70, 0x47, 0xc0, 0x46,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

/* Page data buffer: directly after the 52-byte applet at 0x20001000. */
#define APP_PAGE_BUFFER (AIRSPY_SAMBA_APP_BASE + 0x34u)
#define APP_SLOT_STACK (AIRSPY_SAMBA_APP_BASE + 0x20u)
#define APP_SLOT_RESET (AIRSPY_SAMBA_APP_BASE + 0x24u)
#define APP_SLOT_DST   (AIRSPY_SAMBA_APP_BASE + 0x28u)
#define APP_SLOT_SRC   (AIRSPY_SAMBA_APP_BASE + 0x2cu)
#define APP_SLOT_WORDS (AIRSPY_SAMBA_APP_BASE + 0x30u)

struct airspy_samba {
    airspy_samba_transport_t t;
    uint32_t chip_id;
    bool chip_id_valid;
    bool flash_ready;
};

static int transport_write(const airspy_samba_t *s, const void *data, size_t len)
{
    int rc = s->t.write((const uint8_t *)data, len);
    if (rc != 0)
        return -1;
    return 0;
}

/* Read exactly len bytes. Returns 0 on success, negative on error/timeout. */
static int transport_read(const airspy_samba_t *s, uint8_t *data, size_t len,
                          uint32_t timeout_ms)
{
    size_t got = 0;
    while (got < len) {
        int n = s->t.read(data + got, len - got, timeout_ms);
        if (n < 0)
            return -1;
        if (n == 0)
            return -1; /* timeout */
        got += (size_t)n;
    }
    return 0;
}

static airspy_samba_err_t samba_write_word(airspy_samba_t *s, uint32_t addr, uint32_t value)
{
    char cmd[24];
    int n = snprintf(cmd, sizeof(cmd), "W%08X,%08X#", (unsigned)addr, (unsigned)value);
    return transport_write(s, cmd, (size_t)n) == 0 ? AIRSPY_SAMBA_OK : AIRSPY_SAMBA_ERR_IO;
}

static airspy_samba_err_t samba_read_word(airspy_samba_t *s, uint32_t addr, uint32_t *value)
{
    char cmd[24];
    uint8_t buf[4];
    int n = snprintf(cmd, sizeof(cmd), "w%08X,4#", (unsigned)addr);
    if (transport_write(s, cmd, (size_t)n) != 0)
        return AIRSPY_SAMBA_ERR_IO;
    if (transport_read(s, buf, sizeof(buf), 1000) != 0)
        return AIRSPY_SAMBA_ERR_IO;
    *value = (uint32_t)buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 |
             (uint32_t)buf[3] << 24;
    return AIRSPY_SAMBA_OK;
}

/* Read a single byte (used by the power-of-two read workaround). */
static airspy_samba_err_t samba_read_byte(airspy_samba_t *s, uint32_t addr, uint8_t *value)
{
    char cmd[24];
    int n = snprintf(cmd, sizeof(cmd), "o%08X,4#", (unsigned)addr);
    if (transport_write(s, cmd, (size_t)n) != 0)
        return AIRSPY_SAMBA_ERR_IO;
    if (transport_read(s, value, 1, 1000) != 0)
        return AIRSPY_SAMBA_ERR_IO;
    return AIRSPY_SAMBA_OK;
}

static airspy_samba_err_t samba_write_block(airspy_samba_t *s, uint32_t addr,
                                            const uint8_t *data, size_t len)
{
    char cmd[24];
    int n = snprintf(cmd, sizeof(cmd), "S%08X,%08X#", (unsigned)addr, (unsigned)len);
    if (transport_write(s, cmd, (size_t)n) != 0)
        return AIRSPY_SAMBA_ERR_IO;
    /* Data follows as a separate transport write (a distinct USB packet). */
    if (len && transport_write(s, data, len) != 0)
        return AIRSPY_SAMBA_ERR_IO;
    return AIRSPY_SAMBA_OK;
}

static airspy_samba_err_t samba_read_block(airspy_samba_t *s, uint32_t addr, uint8_t *data,
                                           size_t len)
{
    /* SAM3U ROM monitor bug: reads whose size is a power of two above 32
     * corrupt the first byte over USB. Read the first byte separately. */
    if (len > 32 && !(len & (len - 1))) {
        airspy_samba_err_t e = samba_read_byte(s, addr, data);
        if (e != AIRSPY_SAMBA_OK)
            return e;
        addr += 1;
        data += 1;
        len -= 1;
    }
    char cmd[24];
    int n = snprintf(cmd, sizeof(cmd), "R%08X,%08X#", (unsigned)addr, (unsigned)len);
    if (transport_write(s, cmd, (size_t)n) != 0)
        return AIRSPY_SAMBA_ERR_IO;
    if (transport_read(s, data, len, 2000) != 0)
        return AIRSPY_SAMBA_ERR_IO;
    return AIRSPY_SAMBA_OK;
}

static airspy_samba_err_t samba_go(airspy_samba_t *s, uint32_t addr)
{
    char cmd[24];
    int n = snprintf(cmd, sizeof(cmd), "G%08X#", (unsigned)addr);
    return transport_write(s, cmd, (size_t)n) == 0 ? AIRSPY_SAMBA_OK : AIRSPY_SAMBA_ERR_IO;
}

/* Poll the flash status register until the controller is ready. */
static airspy_samba_err_t flash_wait_ready(airspy_samba_t *s, uint32_t timeout_s)
{
    uint32_t tries = timeout_s * 1000;
    while (tries-- > 0) {
        uint32_t fsr = 0;
        airspy_samba_err_t e = samba_read_word(s, EEFC_FSR, &fsr);
        if (e != AIRSPY_SAMBA_OK)
            return e;
        if (fsr & FSR_CMDE)
            return AIRSPY_SAMBA_ERR_FLASH_CMD;
        if (fsr & FSR_FLOCKE)
            return AIRSPY_SAMBA_ERR_FLASH_LOCK;
        if (fsr & FSR_FLERR)
            return AIRSPY_SAMBA_ERR_FLASH;
        if (fsr & FSR_FRDY)
            return AIRSPY_SAMBA_OK;
        s->t.delay_ms(1);
    }
    return AIRSPY_SAMBA_ERR_TIMEOUT;
}

static airspy_samba_err_t flash_command(airspy_samba_t *s, uint8_t cmd, uint32_t arg)
{
    return samba_write_word(s, EEFC_FCR, FCR(cmd, arg));
}

/* Run the word-copy applet: copy `words` 32-bit words src -> dst. */
static airspy_samba_err_t applet_runv(airspy_samba_t *s)
{
    airspy_samba_err_t e = samba_write_word(s, APP_SLOT_RESET, AIRSPY_SAMBA_APP_BASE + 1);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    /* The SAM-BA "go" reads SP from [APP_SLOT_STACK] and PC from [APP_SLOT_RESET]. */
    return samba_go(s, APP_SLOT_STACK);
}

airspy_samba_t *airspy_samba_create(const airspy_samba_transport_t *transport)
{
    if (!transport || !transport->write || !transport->read || !transport->delay_ms)
        return NULL;
    airspy_samba_t *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->t = *transport;
    return s;
}

void airspy_samba_destroy(airspy_samba_t *s)
{
    free(s);
}

airspy_samba_err_t airspy_samba_connect(airspy_samba_t *s, char version[64])
{
    if (!s)
        return AIRSPY_SAMBA_ERR_BAD_ARG;
    if (version)
        version[0] = 0;

    /* Switch to binary mode. The ROM monitor replies with two bytes ("\n\r"). */
    static const uint8_t binary[] = {'N', '#'};
    if (transport_write(s, binary, sizeof(binary)) != 0)
        return AIRSPY_SAMBA_ERR_IO;
    uint8_t resp[2];
    if (transport_read(s, resp, sizeof(resp), 1000) != 0)
        return AIRSPY_SAMBA_ERR_IO;

    /* Read the bootloader version. */
    if (version) {
        static const uint8_t ver_cmd[] = {'V', '#'};
        if (transport_write(s, ver_cmd, sizeof(ver_cmd)) != 0)
            return AIRSPY_SAMBA_ERR_IO;
        uint8_t buf[64] = {0};
        int n = s->t.read(buf, sizeof(buf) - 1, 200);
        if (n <= 0)
            return AIRSPY_SAMBA_ERR_IO;
        size_t used = 0;
        for (int i = 0; i < n; i++) {
            unsigned char c = buf[i];
            if (c < 0x20 || c > 0x7e)
                break;
            version[used++] = (char)c;
        }
        version[used] = 0;
    }

    /* Identify the chip. */
    uint32_t chip_id = 0;
    airspy_samba_err_t e = samba_read_word(s, SAM3U_CHIPID, &chip_id);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    s->chip_id = chip_id;
    s->chip_id_valid = true;
    return AIRSPY_SAMBA_OK;
}

uint32_t airspy_samba_chip_id(const airspy_samba_t *s)
{
    return s ? s->chip_id : 0;
}

bool airspy_samba_is_sam3u1(const airspy_samba_t *s)
{
    if (!s || !s->chip_id_valid)
        return false;
    /* ATSAM3U1 revisions C (0x28090560) and E (0x28190560). */
    return (s->chip_id & 0x7fffffe0u) == 0x28090560u || (s->chip_id & 0x7fffffe0u) == 0x28190560u;
}

airspy_samba_err_t airspy_samba_flash_init(airspy_samba_t *s)
{
    if (!s)
        return AIRSPY_SAMBA_ERR_BAD_ARG;
    if (s->flash_ready)
        return AIRSPY_SAMBA_OK;

    /* SAM3 errata: force flash wait states to 6. */
    airspy_samba_err_t e = samba_write_word(s, EEFC_FMR, EEFC_FMR_FWS6);
    if (e != AIRSPY_SAMBA_OK)
        return e;

    /* Load the word-copy applet and initialize its data slots. */
    e = samba_write_block(s, AIRSPY_SAMBA_APP_BASE, k_applet, sizeof(k_applet));
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = samba_write_word(s, APP_SLOT_STACK, AIRSPY_SAMBA_APP_STACK);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = samba_write_word(s, APP_SLOT_WORDS, AIRSPY_SAMBA_PAGE_SIZE / 4);
    if (e != AIRSPY_SAMBA_OK)
        return e;

    s->flash_ready = true;
    return AIRSPY_SAMBA_OK;
}

airspy_samba_err_t airspy_samba_erase_all(airspy_samba_t *s)
{
    if (!s)
        return AIRSPY_SAMBA_ERR_BAD_ARG;
    airspy_samba_err_t e = airspy_samba_flash_init(s);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = flash_wait_ready(s, 1);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = flash_command(s, FCMD_EA, 0);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    /* Erase-all can take seconds; poll for up to 30 s. */
    return flash_wait_ready(s, 30);
}

airspy_samba_err_t airspy_samba_write_page(airspy_samba_t *s, uint32_t page,
                                           const uint8_t data[AIRSPY_SAMBA_PAGE_SIZE])
{
    if (!s || page >= AIRSPY_SAMBA_PAGE_COUNT || !data)
        return AIRSPY_SAMBA_ERR_BAD_ARG;
    airspy_samba_err_t e = airspy_samba_flash_init(s);
    if (e != AIRSPY_SAMBA_OK)
        return e;

    e = flash_wait_ready(s, 1);
    if (e != AIRSPY_SAMBA_OK)
        return e;

    /* Stage the page in SRAM, then run the applet to copy it into the flash
     * latch buffer and issue the erase-and-write-page command. */
    e = samba_write_block(s, APP_PAGE_BUFFER, data, AIRSPY_SAMBA_PAGE_SIZE);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = samba_write_word(s, APP_SLOT_DST, AIRSPY_SAMBA_FLASH_BASE + page * AIRSPY_SAMBA_PAGE_SIZE);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = samba_write_word(s, APP_SLOT_SRC, APP_PAGE_BUFFER);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = applet_runv(s);
    if (e != AIRSPY_SAMBA_OK)
        return e;

    return flash_command(s, FCMD_EWP, page);
}

airspy_samba_err_t airspy_samba_read_page(airspy_samba_t *s, uint32_t page,
                                          uint8_t out[AIRSPY_SAMBA_PAGE_SIZE])
{
    if (!s || page >= AIRSPY_SAMBA_PAGE_COUNT || !out)
        return AIRSPY_SAMBA_ERR_BAD_ARG;
    airspy_samba_err_t e = airspy_samba_flash_init(s);
    if (e != AIRSPY_SAMBA_OK)
        return e;

    e = flash_wait_ready(s, 1);
    if (e != AIRSPY_SAMBA_OK)
        return e;

    /* The SAM3U monitor returns zeros for direct flash reads, so copy the page
     * into SRAM first, then read the SRAM buffer. */
    e = samba_write_word(s, APP_SLOT_DST, APP_PAGE_BUFFER);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = samba_write_word(s, APP_SLOT_SRC, AIRSPY_SAMBA_FLASH_BASE + page * AIRSPY_SAMBA_PAGE_SIZE);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = applet_runv(s);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = flash_wait_ready(s, 1);
    if (e != AIRSPY_SAMBA_OK)
        return e;

    return samba_read_block(s, APP_PAGE_BUFFER, out, AIRSPY_SAMBA_PAGE_SIZE);
}

airspy_samba_err_t airspy_samba_set_boot_flash(airspy_samba_t *s)
{
    if (!s)
        return AIRSPY_SAMBA_ERR_BAD_ARG;
    airspy_samba_err_t e = flash_wait_ready(s, 1);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    /* GPNVM bit 1: boot from flash on the next reset. */
    e = flash_command(s, FCMD_SGPB, 1);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    return flash_wait_ready(s, 1);
}

airspy_samba_err_t airspy_samba_read_boot_flash(airspy_samba_t *s, bool *set)
{
    if (!s || !set)
        return AIRSPY_SAMBA_ERR_BAD_ARG;
    airspy_samba_err_t e = flash_wait_ready(s, 1);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = flash_command(s, FCMD_GGPB, 0);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    e = flash_wait_ready(s, 1);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    uint32_t frr = 0;
    e = samba_read_word(s, EEFC_FRR, &frr);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    *set = (frr & (1u << 1)) != 0;
    return AIRSPY_SAMBA_OK;
}

airspy_samba_err_t airspy_samba_read_word(airspy_samba_t *s, uint32_t addr, uint32_t *value)
{
    if (!s || !value)
        return AIRSPY_SAMBA_ERR_BAD_ARG;
    return samba_read_word(s, addr, value);
}

airspy_samba_err_t airspy_samba_reset(airspy_samba_t *s)
{
    if (!s)
        return AIRSPY_SAMBA_ERR_BAD_ARG;
    return samba_write_word(s, SAM3U_WDT_MR, SAM3U_WDT_RESET);
}

airspy_samba_err_t airspy_samba_flash_image(airspy_samba_t *s, const uint8_t *image, size_t len,
                                            airspy_samba_progress_t progress, void *arg)
{
    if (!s || (!image && len) || len > AIRSPY_SAMBA_PAGE_SIZE * AIRSPY_SAMBA_PAGE_COUNT)
        return AIRSPY_SAMBA_ERR_BAD_ARG;

    uint32_t pages = (uint32_t)((len + AIRSPY_SAMBA_PAGE_SIZE - 1) / AIRSPY_SAMBA_PAGE_SIZE);
    if (pages == 0)
        return AIRSPY_SAMBA_ERR_BAD_ARG;
    uint32_t total = pages * AIRSPY_SAMBA_PAGE_SIZE;

    airspy_samba_err_t e = airspy_samba_erase_all(s);
    if (e != AIRSPY_SAMBA_OK)
        return e;

    uint8_t page[AIRSPY_SAMBA_PAGE_SIZE];
    for (uint32_t p = 0; p < pages; p++) {
        size_t offset = (size_t)p * AIRSPY_SAMBA_PAGE_SIZE;
        size_t n = len - offset;
        if (n > AIRSPY_SAMBA_PAGE_SIZE)
            n = AIRSPY_SAMBA_PAGE_SIZE;
        memcpy(page, image + offset, n);
        /* Pad the tail of a partial last page with the flash erase value. */
        if (n < AIRSPY_SAMBA_PAGE_SIZE)
            memset(page + n, 0xFF, AIRSPY_SAMBA_PAGE_SIZE - n);
        e = airspy_samba_write_page(s, p, page);
        if (e != AIRSPY_SAMBA_OK)
            return e;
        if (progress)
            progress((p + 1) * AIRSPY_SAMBA_PAGE_SIZE, total * 2, arg);
    }

    for (uint32_t p = 0; p < pages; p++) {
        e = airspy_samba_read_page(s, p, page);
        if (e != AIRSPY_SAMBA_OK)
            return e;
        size_t offset = (size_t)p * AIRSPY_SAMBA_PAGE_SIZE;
        size_t n = len - offset;
        if (n > AIRSPY_SAMBA_PAGE_SIZE)
            n = AIRSPY_SAMBA_PAGE_SIZE;
        if (memcmp(page, image + offset, n) != 0)
            return AIRSPY_SAMBA_ERR_VERIFY;
        /* The padded tail must read back as 0xFF. */
        for (size_t i = n; i < AIRSPY_SAMBA_PAGE_SIZE; i++) {
            if (page[i] != 0xFF)
                return AIRSPY_SAMBA_ERR_VERIFY;
        }
        if (progress)
            progress(total + (p + 1) * AIRSPY_SAMBA_PAGE_SIZE, total * 2, arg);
    }

    e = airspy_samba_set_boot_flash(s);
    if (e != AIRSPY_SAMBA_OK)
        return e;
    return AIRSPY_SAMBA_OK;
}
