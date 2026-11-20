#include "spy_fft_settings.h"
void spy_fft_settings_init(spy_fft_settings_t *s,uint32_t center)
{
    *s=(spy_fft_settings_t){.center_hz=center,.stage=0,.pixels=2048,.db_offset=0,.db_range=150};
}
spy_fft_setting_result_t spy_fft_settings_apply(spy_fft_settings_t *s,uint32_t setting,uint32_t value,unsigned max_stage)
{
    if(!s)return SPY_FFT_SETTING_INVALID;
    switch(setting) {
    case 200: return value==1?SPY_FFT_SETTING_OK:SPY_FFT_SETTING_INVALID;
    case 201: if(value<500 || value>260000000)return SPY_FFT_SETTING_INVALID;s->center_hz=value;break;
    case 202: if(value>max_stage || value>6)return SPY_FFT_SETTING_INVALID;s->stage=value;break;
    case 203: {
        int64_t signed_value=value<=INT32_MAX?(int64_t)value:(int64_t)value-INT64_C(4294967296);
        if(signed_value < -100 || signed_value > 100)return SPY_FFT_SETTING_INVALID;
        s->db_offset=(int32_t)signed_value;break;
    }
    case 204: if(value<10 || value>150)return SPY_FFT_SETTING_INVALID;s->db_range=value;break;
    case 205: if(value<100 || value>32768)return SPY_FFT_SETTING_INVALID;s->pixels=value;break;
    default: return SPY_FFT_SETTING_UNKNOWN;
    }
    return SPY_FFT_SETTING_OK;
}
