#include <assert.h>
#include <stdio.h>
#include "power_model.h"
int main(void) {
 power_sample_t p;
 power_decode(6080,200,&p); assert(p.valid && p.millivolts==7600 && p.milliamps==100 && p.percent==50 && p.flow==1);
 power_decode(6560,-400,&p); assert(p.valid && p.millivolts==8200 && p.milliamps==-200 && p.percent==90 && p.flow==-1);
 power_decode(6720,0,&p); assert(p.percent==100 && p.flow==0);
 power_decode(5200,10,&p); assert(p.percent==0 && p.flow==0);
 power_decode(0,0,&p); assert(p.valid && p.percent==-1 && p.flow==0);
 power_decode(4000,0,&p); assert(p.percent==-1);
 power_decode(0xffff,0,&p); assert(!p.valid && p.percent==-1);
 power_decode(6080,-32768,&p); assert(p.milliamps==-16384);
 power_decode(6080,32767,&p); assert(p.milliamps==16383);
 power_calibration_t c={.gain_ppm=1000000,.voltage_offset_mv=100,.current_offset_ma=-20,.count=3,.points={{6000,0},{7600,50},{8400,100}}};
 assert(power_calibration_valid(&c));power_decode(6080,20,&p);power_apply_calibration(&p,&c);assert(p.millivolts==7700&&p.milliamps==-10&&p.percent==56&&p.flow==0);
 c.points[1].millivolts=6000;assert(!power_calibration_valid(&c));c.points[1].millivolts=7600;c.gain_ppm=0;assert(!power_calibration_valid(&c));
 puts("Power: INA226 units/sign, 2S estimate/clamping, idle and unavailable readings passed (ASan/UBSan).");
}
