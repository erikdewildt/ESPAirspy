#include "airspy_update_check.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* Expected file name for each variant. */
    assert(strcmp(airspy_expected_image_name("R5.0.0-CD"),"hfplus-firmware-cd.bin")==0);
    assert(strcmp(airspy_expected_image_name("R5.0.0-BB"),"hfplus-firmware-bb.bin")==0);
    assert(strcmp(airspy_expected_image_name("R4.0.8-CD"),"hfplus-firmware-cd.bin")==0);
    assert(strcmp(airspy_expected_image_name(""),"")==0);
    assert(strcmp(airspy_expected_image_name("R5.0.0"),"")==0);
    assert(strcmp(airspy_expected_image_name(NULL),"")==0);

    /* Correct image for the receiver. */
    assert(airspy_update_check("hfplus-firmware-cd.bin","R5.0.0-CD")==AIRSPY_IMAGE_OK);
    assert(airspy_update_check("hfplus-firmware-bb.bin","R5.0.0-BB")==AIRSPY_IMAGE_OK);

    /* Wrong image for the receiver. */
    assert(airspy_update_check("hfplus-firmware-bb.bin","R5.0.0-CD")==AIRSPY_IMAGE_WRONG_IMAGE);
    assert(airspy_update_check("hfplus-firmware-cd.bin","R5.0.0-BB")==AIRSPY_IMAGE_WRONG_IMAGE);

    /* Unknown receiver variant. */
    assert(airspy_update_check("hfplus-firmware-cd.bin","R5.0.0")==AIRSPY_IMAGE_WRONG_VARIANT);
    assert(airspy_update_check("hfplus-firmware-cd.bin","")==AIRSPY_IMAGE_WRONG_VARIANT);

    /* File name that does not identify an image. */
    assert(airspy_update_check("firmware.bin","R5.0.0-CD")==AIRSPY_IMAGE_UNKNOWN);
    assert(airspy_update_check(NULL,"R5.0.0-CD")==AIRSPY_IMAGE_UNKNOWN);

    /* Case-insensitive and renamed-but-variant-preserved. */
    assert(airspy_update_check("HFPPLUS-FIRMWARE-CD.BIN","R5.0.0-CD")==AIRSPY_IMAGE_OK);
    assert(airspy_update_check("my-cd-firmware.bin","R5.0.0-CD")==AIRSPY_IMAGE_OK);
    assert(airspy_update_check("airspy-bb.bin","R5.0.0-BB")==AIRSPY_IMAGE_OK);

    puts("Airspy update check: BB/CD file name and variant validation passed");
    return 0;
}
