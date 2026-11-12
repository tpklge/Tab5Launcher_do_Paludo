#include "time_mgr.h"
#include <stdio.h>
/* Explicit Portuguese names: independent of the embedded C locale. */
void time_date_pt(const struct tm *t, char *out, size_t len) {
  static const char *const days[] = {
      "Domingo",      "Segunda-feira", "Terça-feira", "Quarta-feira",
      "Quinta-feira", "Sexta-feira",   "Sábado"};
  static const char *const months[] = {
      "janeiro", "fevereiro", "março",    "abril",   "maio",     "junho",
      "julho",   "agosto",    "setembro", "outubro", "novembro", "dezembro"};
  if (!out || !len)
    return;
  if (!t || t->tm_wday < 0 || t->tm_wday > 6 || t->tm_mon < 0 ||
      t->tm_mon > 11 || t->tm_mday < 1 || t->tm_mday > 31) {
    snprintf(out, len, "Ajuste data e hora em CFG");
    return;
  }
  snprintf(out, len, "%s\n%d de %s de %d", days[t->tm_wday], t->tm_mday,
           months[t->tm_mon], t->tm_year + 1900);
}
