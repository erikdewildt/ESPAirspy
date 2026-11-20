#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * Orchestrates an Airspy HF+ Discovery firmware update end to end: reboot the
 * receiver into its SAM-BA bootloader, erase/write/verify the uploaded image,
 * set the boot-from-flash bit, reset back into the application and restore the
 * saved factory calibration. The state machine is advanced by
 * airspy_update_poll(), which the USB client task calls each iteration so that
 * device re-enumeration events and blocking SAM-BA transfers share the same
 * context.
 */

typedef enum {
    AIRSPY_UPDATE_IDLE = 0,
    AIRSPY_UPDATE_REBOOTING,     /* sent reboot-to-SAM-BA */
    AIRSPY_UPDATE_WAIT_SAMBA,    /* waiting for 03eb:6124 */
    AIRSPY_UPDATE_PROGRAMMING,   /* erase/write/verify in progress */
    AIRSPY_UPDATE_BOOTING,       /* reset sent, waiting for 03eb:800c */
    AIRSPY_UPDATE_RECOVERING,    /* setting boot-from-flash + reset (no flash) */
    AIRSPY_UPDATE_DONE,
    AIRSPY_UPDATE_FAILED,
} airspy_update_state_t;

typedef struct {
    airspy_update_state_t state;
    uint8_t progress;    /* 0..100 during PROGRAMMING */
    const char *message; /* human-readable status (static string) */
} airspy_update_status_t;

/* The uploaded image is staged in an unused flash region (the internal RAM has
 * too little free heap for a full-image buffer). These helpers let the web
 * handler stream the upload directly into that scratch area. */
#define AIRSPY_UPDATE_SCRATCH_OFFSET 0xf00000u
#define AIRSPY_UPDATE_SCRATCH_SIZE    0x10000u

/* Erase the scratch region before streaming a new image into it. */
esp_err_t airspy_update_scratch_prepare(void);
/* Append len bytes at offset within the (already erased) scratch region. */
esp_err_t airspy_update_scratch_write(size_t offset, const void *data, size_t len);

/* Queue an image (already staged in the scratch region and verified by the
 * caller) for programming. Returns true if accepted (no update in progress). */
bool airspy_update_submit(size_t len);

/* Request recovery of a receiver left in SAM-BA mode: set the boot-from-flash
 * bit and reset, without touching the firmware or the calibration page. */
void airspy_recover_request(void);

/* Advance the update state machine. Call from the USB client task loop. */
void airspy_update_poll(void);

bool airspy_update_in_progress(void);

void airspy_update_status(airspy_update_status_t *out);
