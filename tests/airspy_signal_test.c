#include "airspy_signal.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const int16_t iq[]={0,0, 32759,-32759, 32760,0, 0,-32760, 32767,-32768, -32768,32767};
    assert(airspy_iq_near_rail(iq,6)==4);
    assert(airspy_iq_near_rail(iq,2)==0);
    assert(airspy_iq_near_rail(iq,0)==0);
    puts("Airspy input levels: threshold, signs and one count per I/Q pair passed");
}
