#include "airspy_signal.h"
uint32_t airspy_iq_near_rail(const int16_t *iq,size_t samples)
{
    uint32_t n=0;
    for(size_t i=0;i<samples;i++) {
        int32_t a=iq[2*i],b=iq[2*i+1];
        n+=(a>=32760 || a<=-32760 || b>=32760 || b<=-32760);
    }
    return n;
}
