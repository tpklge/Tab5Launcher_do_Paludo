#include "power_model.h"
void power_decode(uint16_t bus_raw, int16_t shunt_raw, power_sample_t *out) {
  *out = (power_sample_t){.valid = true,
                          .millivolts = (int)bus_raw * 5 / 4,
                          .milliamps = (int)shunt_raw / 2,
                          .percent = -1};
  if (out->millivolts < 0 || out->millivolts > 36000) {
    out->valid = false;
    return;
  }
  out->flow = out->milliamps > 10 ? 1 : out->milliamps < -10 ? -1 : 0;
  /* 2S voltage estimate, not a fuel gauge. Under-load/charging error is
   * expected. */
  if (out->millivolts >= 5500 && out->millivolts <= 9000) {
    int cell = out->millivolts / 2;
    static const int mv[] = {3300, 3500, 3600, 3700, 3750,
                             3800, 3900, 4000, 4100, 4200};
    static const int pc[] = {0, 5, 10, 20, 35, 50, 65, 80, 90, 100};
    if (cell <= mv[0])
      out->percent = 0;
    else if (cell >= mv[9])
      out->percent = 100;
    else
      for (unsigned i = 1; i < 10; i++)
        if (cell <= mv[i]) {
          out->percent = pc[i - 1] + (cell - mv[i - 1]) * (pc[i] - pc[i - 1]) /
                                         (mv[i] - mv[i - 1]);
          break;
        }
  }
}
bool power_calibration_valid(const power_calibration_t *c) {
  if (!c || c->gain_ppm < 900000 || c->gain_ppm > 1100000 ||
      c->voltage_offset_mv < -500 || c->voltage_offset_mv > 500 ||
      c->current_offset_ma < -100 || c->current_offset_ma > 100 ||
      c->count > POWER_CURVE_POINTS || c->count == 1)
    return false;
  for (unsigned i = 0; i < c->count; i++) {
    if (c->points[i].millivolts < 6000 || c->points[i].millivolts > 9000 ||
        c->points[i].percent < 0 || c->points[i].percent > 100)
      return false;
    if (i && (c->points[i].millivolts <= c->points[i - 1].millivolts ||
              c->points[i].percent <= c->points[i - 1].percent))
      return false;
  }
  return !c->count ||
         (c->points[0].percent == 0 && c->points[c->count - 1].percent == 100);
}
void power_apply_calibration(power_sample_t *p, const power_calibration_t *c) {
  if (!p || !p->valid || !power_calibration_valid(c))
    return;
  p->millivolts = (int)((int64_t)p->millivolts * c->gain_ppm / 1000000) +
                  c->voltage_offset_mv;
  if(p->millivolts<0||p->millivolts>36000){p->valid=false;p->percent=-1;return;}
  p->milliamps += c->current_offset_ma;
  p->flow = p->milliamps > 10 ? 1 : p->milliamps < -10 ? -1 : 0;
  power_sample_t estimate;
  power_decode((uint16_t)((int64_t)p->millivolts * 4 / 5), 0, &estimate);
  p->percent = estimate.percent;
  if (!c->count || p->millivolts < 5500 || p->millivolts > 9000)
    return;
  if (p->millivolts <= c->points[0].millivolts) {
    p->percent = 0;
    return;
  }
  if (p->millivolts >= c->points[c->count - 1].millivolts) {
    p->percent = 100;
    return;
  }
  for (unsigned i = 1; i < c->count; i++)
    if (p->millivolts <= c->points[i].millivolts) {
      power_curve_point_t a = c->points[i - 1], b = c->points[i];
      p->percent = a.percent + (p->millivolts - a.millivolts) *
                                   (b.percent - a.percent) /
                                   (b.millivolts - a.millivolts);
      return;
    }
}
