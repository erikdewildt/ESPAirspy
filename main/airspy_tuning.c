#include "airspy_tuning.h"
#include <math.h>
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
bool airspy_parse_calibration(const uint8_t *data,size_t size,int32_t *ppb)
{
    if(size<16 || le32(data)!=0xa5ca71b0u)return false;
    uint32_t raw=le32(data+4);
    int64_t value=raw<=INT32_MAX?(int64_t)raw:(int64_t)raw-0x100000000LL;
    /* A correction beyond 1000 ppm is not a credible oscillator calibration. */
    if(value < -1000000 || value > 1000000)return false;
    *ppb=(int32_t)value;return true;
}
double airspy_adjusted_frequency(uint32_t requested_hz,int32_t ppb)
{
    return requested_hz*(1.0+(double)ppb*1e-9);
}
bool airspy_parse_frequency_delta(const uint8_t *data,size_t size,double *hz)
{
    /* Older Discovery firmware returns one byte: no frequency-delta support. */
    if(size!=4 || data[0]>31)return false;
    int32_t value=(int32_t)((uint32_t)data[1]|(uint32_t)data[2]<<8|(uint32_t)data[3]<<16);
    if(value&0x800000)value-=0x1000000;
    *hz=ldexp((double)value*1000.0,-data[0]);return true;
}
