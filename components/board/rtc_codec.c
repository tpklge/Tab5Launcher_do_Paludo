#include "rtc_codec.h"
static int bcd(uint8_t n) {
  return (n & 15) > 9 || (n >> 4) > 9 ? -1 : (n >> 4) * 10 + (n & 15);
}
static uint8_t encode(int n) { return (n / 10) * 16 + n % 10; }
bool rtc_decode(const uint8_t d[7], time_t *utc) {
  int sec = bcd(d[0]), min = bcd(d[1]), hour = bcd(d[2]), day = bcd(d[4]),
      month = bcd(d[5]), year = bcd(d[6]);
  if (sec < 0 || sec > 59 || min < 0 || min > 59 || hour < 0 || hour > 23 ||
      month < 1 || month > 12 || day < 1 || year < 0 || !d[3] || d[3] > 64 ||
      (d[3] & (d[3] - 1)))
    return false;
  const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day > days[month - 1] + (month == 2 && year % 4 == 0))
    return false;
  int64_t total = 10957; /* 1970 -> 2000 */
  for (int y = 0; y < year; y++)
    total += 365 + (y % 4 == 0);
  for (int m = 1; m < month; m++)
    total += days[m - 1] + (m == 2 && year % 4 == 0);
  *utc = (time_t)((total + day - 1) * 86400 + hour * 3600 + min * 60 + sec);
  return true;
}
bool rtc_encode(time_t utc, uint8_t d[7]) {
  struct tm t;
  if (!gmtime_r(&utc, &t) || t.tm_year < 100 || t.tm_year > 199)
    return false;
  d[0] = encode(t.tm_sec);
  d[1] = encode(t.tm_min);
  d[2] = encode(t.tm_hour);
  d[3] = 1u << t.tm_wday;
  d[4] = encode(t.tm_mday);
  d[5] = encode(t.tm_mon + 1);
  d[6] = encode(t.tm_year - 100);
  return true;
}
