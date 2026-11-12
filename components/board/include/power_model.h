#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool valid;
    int millivolts;
    int milliamps; /* positive = discharging, negative = charging on the Tab5 shunt */
    int percent;  /* voltage-based 2S estimate, -1 when unavailable */
    int flow;     /* -1 charging, 0 idle, +1 discharging; not a USB-presence claim */
} power_sample_t;
void power_decode(uint16_t bus_raw,int16_t shunt_raw,power_sample_t *out);
#define POWER_CURVE_POINTS 16
typedef struct {int millivolts,percent;} power_curve_point_t;
typedef struct {int gain_ppm,voltage_offset_mv,current_offset_ma;unsigned count;power_curve_point_t points[POWER_CURVE_POINTS];} power_calibration_t;
bool power_calibration_valid(const power_calibration_t *);
void power_apply_calibration(power_sample_t *,const power_calibration_t *);
