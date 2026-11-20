#include "spy_fft_settings.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    spy_fft_settings_t s;spy_fft_settings_init(&s,7074000);
    assert(s.center_hz==7074000 && s.stage==0 && s.pixels==2048 && s.db_range==150 && s.db_offset==0);
    assert(spy_fft_settings_apply(&s,200,1,6)==SPY_FFT_SETTING_OK);
    assert(spy_fft_settings_apply(&s,200,2,6)==SPY_FFT_SETTING_INVALID);
    assert(spy_fft_settings_apply(&s,201,7100000,6)==SPY_FFT_SETTING_OK && s.center_hz==7100000);
    assert(spy_fft_settings_apply(&s,201,499,6)==SPY_FFT_SETTING_INVALID && s.center_hz==7100000);
    assert(spy_fft_settings_apply(&s,202,6,6)==SPY_FFT_SETTING_OK && s.stage==6);
    assert(spy_fft_settings_apply(&s,202,7,7)==SPY_FFT_SETTING_INVALID && s.stage==6);
    assert(spy_fft_settings_apply(&s,203,(uint32_t)-100,6)==SPY_FFT_SETTING_OK && s.db_offset==-100);
    assert(spy_fft_settings_apply(&s,203,(uint32_t)-101,6)==SPY_FFT_SETTING_INVALID && s.db_offset==-100);
    assert(spy_fft_settings_apply(&s,203,100,6)==SPY_FFT_SETTING_OK && s.db_offset==100);
    assert(spy_fft_settings_apply(&s,203,101,6)==SPY_FFT_SETTING_INVALID && s.db_offset==100);
    assert(spy_fft_settings_apply(&s,204,10,6)==SPY_FFT_SETTING_OK && s.db_range==10);
    assert(spy_fft_settings_apply(&s,204,151,6)==SPY_FFT_SETTING_INVALID && s.db_range==10);
    assert(spy_fft_settings_apply(&s,205,32768,6)==SPY_FFT_SETTING_OK && s.pixels==32768);
    assert(spy_fft_settings_apply(&s,205,32769,6)==SPY_FFT_SETTING_INVALID && s.pixels==32768);
    assert(spy_fft_settings_apply(&s,206,0,6)==SPY_FFT_SETTING_UNKNOWN);
    assert(spy_fft_settings_apply(NULL,200,1,6)==SPY_FFT_SETTING_INVALID);
    puts("FFT settings: formats, signed dB, boundaries and invalid updates passed");
}
