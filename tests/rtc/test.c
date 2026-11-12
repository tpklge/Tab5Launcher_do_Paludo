#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rtc_codec.h"
int main(void)
{
 uint8_t raw[7]; time_t back;
 for(time_t t=946684800;t<4102444800LL;t+=86400) { assert(rtc_encode(t,raw)); assert(rtc_decode(raw,&back)&&back==t); }
 uint8_t leap[]={0,0,0,1,0x29,0x02,0x24}; assert(rtc_decode(leap,&back));
 leap[6]=0x23; assert(!rtc_decode(leap,&back)); leap[6]=0x24; leap[0]=0x6a; assert(!rtc_decode(leap,&back));
 leap[0]=0; leap[3]=0; assert(!rtc_decode(leap,&back));
 assert(!rtc_encode(0,raw)); assert(!rtc_encode(4102444800LL,raw));
 puts("RTC: full 2000–2099 calendar roundtrips, leap years, invalid BCD/weekdays and year bounds passed (ASan/UBSan).");
}
