#include "airspy_update_check.h"
#include <ctype.h>
#include <string.h>

static const char k_cd_name[] = "hfplus-firmware-cd.bin";
static const char k_bb_name[] = "hfplus-firmware-bb.bin";

/* 1 = CD, 2 = BB, 0 = unknown. */
static int variant_of_version(const char *version)
{
    if(!version)return 0;
    size_t n=strlen(version);
    if(n<3)return 0;
    if(!strcmp(version+n-3,"-CD"))return 1;
    if(!strcmp(version+n-3,"-BB"))return 2;
    return 0;
}

/* 1 = CD, 2 = BB, 0 = unknown, detected from a "-cd"/"-bb" token in the name. */
static int variant_of_filename(const char *filename)
{
    if(!filename)return 0;
    size_t n=strlen(filename);
    for(size_t i=0;i+3<=n;i++) {
        char a=(char)tolower((unsigned char)filename[i]);
        char b=(char)tolower((unsigned char)filename[i+1]);
        char c=(char)tolower((unsigned char)filename[i+2]);
        if(a=='-' && b=='c' && c=='d')return 1;
        if(a=='-' && b=='b' && c=='b')return 2;
    }
    return 0;
}

const char *airspy_expected_image_name(const char *version)
{
    switch(variant_of_version(version)) {
    case 1: return k_cd_name;
    case 2: return k_bb_name;
    default: return "";
    }
}

airspy_image_check_t airspy_update_check(const char *filename,const char *version)
{
    if(!filename)return AIRSPY_IMAGE_UNKNOWN;
    int want=variant_of_version(version);
    if(want==0)return AIRSPY_IMAGE_WRONG_VARIANT;
    int got=variant_of_filename(filename);
    if(got==0)return AIRSPY_IMAGE_UNKNOWN;
    if(got!=want)return AIRSPY_IMAGE_WRONG_IMAGE;
    return AIRSPY_IMAGE_OK;
}
