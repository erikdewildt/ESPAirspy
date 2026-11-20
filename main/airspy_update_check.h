#pragma once
#include <stddef.h>
#include <stdint.h>

/*
 * The Airspy HF+ Discovery ships with two tuner silicon revisions (BB and CD).
 * The firmware package selects them by file name: hfplus-firmware-cd.bin vs
 * hfplus-firmware-bb.bin. The receiver's reported version suffix (-BB/-CD)
 * tells which one it needs. Validating on the file name stays correct across
 * firmware releases, unlike a hardcoded image hash.
 */

/* The receiver's firmware must not overwrite the calibration page (page 255),
 * so 255 pages of 256 bytes is the hard limit. */
#define AIRSPY_IMAGE_MAX_SIZE (255u * 256u)

typedef enum {
    AIRSPY_IMAGE_OK,           /* file name matches the receiver's variant */
    AIRSPY_IMAGE_WRONG_VARIANT,/* receiver reports neither -BB nor -CD */
    AIRSPY_IMAGE_WRONG_IMAGE,  /* file name is for the other variant */
    AIRSPY_IMAGE_UNKNOWN,      /* file name does not identify an Airspy image */
} airspy_image_check_t;

/* Expected image file name for the receiver's reported version, e.g.
 * "hfplus-firmware-cd.bin", or "" when the variant is unknown. */
const char *airspy_expected_image_name(const char *version);

/* Validate an uploaded file name against the receiver's reported version. */
airspy_image_check_t airspy_update_check(const char *filename, const char *version);
