#include "airspy_tuning.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void)
{
    const uint8_t config[16]={0xb0,0x71,0xca,0xa5,0x68,0xff,0xff,0xff};
    int32_t ppb=0;
    assert(airspy_parse_calibration(config,sizeof(config),&ppb) && ppb==-152);
    assert(!airspy_parse_calibration(config,7,&ppb));
    uint8_t bad[16]={0};assert(!airspy_parse_calibration(bad,16,&ppb));
    const uint8_t excessive[16]={0xb0,0x71,0xca,0xa5,0xff,0xff,0xff,0x7f};
    assert(!airspy_parse_calibration(excessive,16,&ppb));
    assert(fabs(airspy_adjusted_frequency(7100000,-152)-7099998.9208)<1e-6);
    assert(fabs(airspy_adjusted_frequency(260000000,-152)-259999960.48)<1e-6);
    assert(airspy_adjusted_frequency(7100000,0)==7100000);
    const uint8_t positive[]={8,0,4,0},negative[]={4,0xf0,0xff,0xff},invalid[]={32,0,0,0};
    double hz=0;
    assert(airspy_parse_frequency_delta(positive,4,&hz) && hz==4000);
    assert(airspy_parse_frequency_delta(negative,4,&hz) && hz==-1000);
    assert(!airspy_parse_frequency_delta(positive,1,&hz));
    assert(!airspy_parse_frequency_delta(invalid,4,&hz));
    puts("Airspy tuning: factory record, signed ppb, frequency scaling and signed/short delta replies passed");
}
