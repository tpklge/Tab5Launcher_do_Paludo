#pragma once
#include <string.h>
#define LAUNCHER_THEME_COUNT 9
#define LAUNCHER_THEME_OPTIONS                                                 \
  "Graphite\nMidnight\nDaylight\nNeon\nOcean\nForest\nSunset\nLavender\nSand"
static inline const char *launcher_theme_name(unsigned index) {
  static const char *const names[] = {"graphite", "midnight", "daylight",
                                      "neon",     "ocean",    "forest",
                                      "sunset",   "lavender", "sand"};
  return names[index < LAUNCHER_THEME_COUNT ? index : 0];
}
static inline unsigned launcher_theme_index(const char *name) {
  for (unsigned i = 0; name && i < LAUNCHER_THEME_COUNT; ++i)
    if (!strcmp(name, launcher_theme_name(i)))
      return i;
  return 0;
}
static inline int launcher_theme_valid(const char *name) {
  return name && !strcmp(name, launcher_theme_name(launcher_theme_index(name)));
}
