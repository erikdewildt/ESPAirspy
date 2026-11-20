#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * SAM-BA bootloader protocol and EEFC flash programming for the Atmel
 * ATSAM3U1 inside the Airspy HF+ Discovery. This module is transport-agnostic:
 * the byte transport (USB CDC bulk endpoints on the ESP32 host) is injected so
 * the exact command sequences can be exercised in a host test.
 *
 * The command framing and flash algorithm mirror BOSSA (the reference `bossac`
 * flasher proven on this receiver), including the word-copy applet used to
 * move page data between SRAM and flash, and the EEFC register layout.
 */

/* ATSAM3U1 flash geometry. */
#define AIRSPY_SAMBA_FLASH_BASE 0x00080000u
#define AIRSPY_SAMBA_PAGE_SIZE  256u
#define AIRSPY_SAMBA_PAGE_COUNT 256u

/* SRAM locations used by the word-copy applet. */
#define AIRSPY_SAMBA_APP_BASE 0x20001000u
#define AIRSPY_SAMBA_APP_STACK 0x20002000u

/* Byte transport to the receiver's SAM-BA bootloader. */
typedef struct {
    /* Write all len bytes. Returns 0 on success, negative on error. */
    int (*write)(const uint8_t *data, size_t len);
    /* Read up to len bytes before timeout_ms. Returns the number of bytes
     * read (>= 0), 0 on timeout, or negative on error. */
    int (*read)(uint8_t *data, size_t len, uint32_t timeout_ms);
    /* Block for at least ms milliseconds (used while polling flash status). */
    void (*delay_ms)(uint32_t ms);
} airspy_samba_transport_t;

typedef enum {
    AIRSPY_SAMBA_OK = 0,
    AIRSPY_SAMBA_ERR_IO = -1,
    AIRSPY_SAMBA_ERR_TIMEOUT = -2,
    AIRSPY_SAMBA_ERR_FLASH_CMD = -3,
    AIRSPY_SAMBA_ERR_FLASH_LOCK = -4,
    AIRSPY_SAMBA_ERR_FLASH = -5,
    AIRSPY_SAMBA_ERR_UNSUPPORTED = -6,
    AIRSPY_SAMBA_ERR_VERIFY = -7,
    AIRSPY_SAMBA_ERR_BAD_ARG = -8,
} airspy_samba_err_t;

typedef struct airspy_samba airspy_samba_t;

airspy_samba_t *airspy_samba_create(const airspy_samba_transport_t *transport);
void airspy_samba_destroy(airspy_samba_t *s);

/* Enter binary mode, read the bootloader version and the chip id. */
airspy_samba_err_t airspy_samba_connect(airspy_samba_t *s, char version[64]);
uint32_t airspy_samba_chip_id(const airspy_samba_t *s);
/* True when the chip id identifies an ATSAM3U1 (the Airspy HF+ Discovery MCU). */
bool airspy_samba_is_sam3u1(const airspy_samba_t *s);

/* Load the word-copy applet and configure the flash wait states (idempotent). */
airspy_samba_err_t airspy_samba_flash_init(airspy_samba_t *s);

airspy_samba_err_t airspy_samba_erase_all(airspy_samba_t *s);
airspy_samba_err_t airspy_samba_write_page(airspy_samba_t *s, uint32_t page,
                                           const uint8_t data[AIRSPY_SAMBA_PAGE_SIZE]);
airspy_samba_err_t airspy_samba_read_page(airspy_samba_t *s, uint32_t page,
                                          uint8_t out[AIRSPY_SAMBA_PAGE_SIZE]);
/* Set GPNVM bit 1 so the next reset boots the application from flash. */
airspy_samba_err_t airspy_samba_set_boot_flash(airspy_samba_t *s);
/* Read back GPNVM bit 1 (boot-from-flash). */
airspy_samba_err_t airspy_samba_read_boot_flash(airspy_samba_t *s, bool *set);
/* Read a 32-bit word from the target (diagnostic). */
airspy_samba_err_t airspy_samba_read_word(airspy_samba_t *s, uint32_t addr, uint32_t *value);
/* Force a watchdog reset back into the application. */
airspy_samba_err_t airspy_samba_reset(airspy_samba_t *s);

typedef void (*airspy_samba_progress_t)(uint32_t done, uint32_t total, void *arg);

/* Erase, write (zero-padding a partial last page to 0xFF) and verify an image.
 * Reports cumulative bytes across write and verify phases. */
airspy_samba_err_t airspy_samba_flash_image(airspy_samba_t *s, const uint8_t *image,
                                            size_t len, airspy_samba_progress_t progress,
                                            void *arg);
