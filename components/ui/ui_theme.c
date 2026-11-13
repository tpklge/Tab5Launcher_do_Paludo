#include "lvgl.h"
#include "storage_guard.h"
#include "ui.h"
#include <string.h>
typedef struct {
  uint32_t bg, panel, text, muted, border;
} palette_t;
static const palette_t s_palettes[] = {
    {0x171A20, 0x242830, 0xEEF2F8, 0xA3ADBD, 0x454C58},
    {0x0D1225, 0x18233D, 0xEDF2FF, 0xA6B6D4, 0x334B74},
    {0xEDF1F7, 0xFFFFFF, 0x142238, 0x526176, 0xB7C2D3},
    {0x050509, 0x12121D, 0xF7F2FF, 0xB1A6CB, 0x664D88},
    {0x071E2B, 0x103447, 0xE5F7FF, 0xA0C9D9, 0x356275},
    {0x101F18, 0x1D3528, 0xEDF8EF, 0xB0CEB6, 0x466B52},
    {0x29151D, 0x42232D, 0xFFF0E7, 0xE2B8AE, 0x85515E},
    {0xF0EAF8, 0xFCF9FF, 0x30233F, 0x695778, 0xB9A5CC},
    {0xF1E9DB, 0xFFFAF0, 0x3D3023, 0x74624C, 0xBFA98A}};
_Static_assert(sizeof(s_palettes) / sizeof(*s_palettes) == UI_THEME_COUNT,
               "Theme palettes must match the enum");
static unsigned s_theme;
static uint32_t s_accent = 0xEB2638;
void ui_theme_tree(lv_obj_t *o) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_USER_1))
    return; /* fixed clock palette */
  const palette_t *p = &s_palettes[s_theme];
  uint32_t ink =
      (((s_accent >> 16) & 255) * 299 + ((s_accent >> 8) & 255) * 587 +
       (s_accent & 255) * 114) > 150000
          ? 0x101010
          : 0xFFFFFF;
  lv_obj_set_style_border_color(o, lv_color_hex(s_accent), LV_STATE_CHECKED);
  bool button = lv_obj_check_type(o, &lv_button_class),
       label = lv_obj_check_type(o, &lv_label_class);
  bool primary = lv_obj_has_flag(o, LV_OBJ_FLAG_USER_2) ||
                 lv_obj_has_state(o, LV_STATE_CHECKED);
  bool danger = lv_obj_has_flag(o, LV_OBJ_FLAG_USER_3);
  if (label) {
    lv_obj_t *parent = lv_obj_get_parent(o);
    bool on_button = parent && lv_obj_check_type(parent, &lv_button_class);
    bool highlighted =
        on_button && (lv_obj_has_flag(parent, LV_OBJ_FLAG_USER_2) ||
                      lv_obj_has_state(parent, LV_STATE_CHECKED));
    bool destructive = on_button && lv_obj_has_flag(parent, LV_OBJ_FLAG_USER_3);
    lv_obj_set_style_text_color(o,
                                lv_color_hex(destructive   ? 0xD33348
                                             : highlighted ? ink
                                                           : p->text),
                                0);
  } else if (!lv_obj_check_type(o, &lv_image_class)) {
    lv_obj_set_style_bg_color(
        o, lv_color_hex(button ? (primary ? s_accent : p->bg) : p->panel), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(p->border), 0);
    lv_obj_set_style_text_color(o, lv_color_hex(p->text), 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(s_accent), LV_PART_SCROLLBAR);
    if (button) {
      lv_obj_set_style_bg_color(o, lv_color_hex(s_accent), LV_STATE_CHECKED);
      lv_obj_set_style_text_color(o,
                                  lv_color_hex(danger    ? 0xD33348
                                               : primary ? ink
                                                         : p->text),
                                  0);
      if (danger)
        lv_obj_set_style_border_color(o, lv_color_hex(0xD33348), 0);
    }
    if (lv_obj_check_type(o, &lv_bar_class) ||
        lv_obj_check_type(o, &lv_slider_class) ||
        lv_obj_check_type(o, &lv_switch_class) ||
        lv_obj_check_type(o, &lv_checkbox_class) ||
        lv_obj_check_type(o, &lv_arc_class)) {
      lv_obj_set_style_bg_color(o, lv_color_hex(s_accent), LV_PART_INDICATOR);
      lv_obj_set_style_arc_color(o, lv_color_hex(s_accent), LV_PART_INDICATOR);
    }
    if (lv_obj_check_type(o, &lv_checkbox_class)) {
      lv_obj_set_style_pad_all(o, 8, 0);
      lv_obj_set_style_pad_column(o, 14, 0);
      lv_obj_set_style_pad_all(o, 7, LV_PART_INDICATOR);
      lv_obj_set_ext_click_area(o, 8);
    }
    if (lv_obj_check_type(o, &lv_switch_class)) {
      lv_obj_set_style_bg_color(o, lv_color_hex(p->border), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    }
    if (lv_obj_check_type(o, &lv_slider_class)) {
      lv_obj_set_style_bg_color(o, lv_color_hex(p->border), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_bg_color(o, lv_color_hex(s_accent), LV_PART_KNOB);
    }
    if (lv_obj_check_type(o, &lv_keyboard_class)) {
      lv_obj_set_style_bg_color(o, lv_color_hex(p->bg), LV_PART_ITEMS);
      lv_obj_set_style_text_color(o, lv_color_hex(p->text), LV_PART_ITEMS);
    }
  }
  lv_obj_set_style_outline_color(o, lv_color_hex(s_accent), LV_STATE_FOCUS_KEY);
  for (unsigned i = 0; i < lv_obj_get_child_count(o); i++)
    ui_theme_tree(lv_obj_get_child(o, i));
}
void ui_theme_apply(ui_theme_t theme, uint32_t accent) {
  s_theme = theme >= 0 && theme < UI_THEME_COUNT ? theme : UI_THEME_GRAPHITE;
  s_accent = accent & 0xffffff;
  lv_obj_t *root = lv_screen_active();
  if (!root)
    return;
  ui_theme_tree(root);
  ui_theme_tree(lv_layer_top());
  lv_obj_set_style_bg_color(root, lv_color_hex(s_palettes[s_theme].bg), 0);
}
lv_color_t ui_text_color(void) {
  return lv_color_hex(s_palettes[s_theme].text);
}

LV_FONT_DECLARE(ui_latin20);
static lv_font_t latin;
static bool fonts_initialized;
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
static _Atomic unsigned font_state;
static uint8_t *font_encoded[2];
static uint32_t font_lengths[2];
static void font_worker(void *unused) {
  if (!storage_io_acquire()) {
    atomic_store(&font_state, 3);
    vTaskDelete(NULL);
    return;
  }
  const char *paths[] = {"/sdcard/launcher/fonts/cjk.bin",
                         "/sdcard/launcher/fonts/emoji.bin"};
  const char *hashes[] = {
      "21d7913e6ccc17d4f37e35a65d48795f5bdbd199220712254fe9146b1c3cda96",
      "b960e3196cb8d9b8753478e66565df5eddfbe52c9e322f1784c85a303ed23f5a"};
  for (unsigned i = 0; i < 2; i++) {
    FILE *f = fopen(paths[i], "rb");
    if (!f)
      continue;
    struct stat st;
    bool ok =
        !fstat(fileno(f), &st) && st.st_size > 32 && st.st_size <= 8 * 1048576;
    uint8_t *raw =
        ok ? heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
           : NULL;
    ok = ok && raw;
    mbedtls_sha256_context h;
    mbedtls_sha256_init(&h);
    mbedtls_sha256_starts(&h, 0);
    for (size_t off = 0; ok && off < (size_t)st.st_size;) {
      size_t n = st.st_size - off < 65536 ? st.st_size - off : 65536;
      if (fread(raw + off, 1, n, f) != n) {
        ok = false;
        break;
      }
      mbedtls_sha256_update(&h, raw + off, n);
      off += n;
      vTaskDelay(1);
    }
    uint8_t digest[32];
    char hex[65];
    mbedtls_sha256_finish(&h, digest);
    mbedtls_sha256_free(&h);
    fclose(f);
    for (unsigned k = 0; k < 32; k++)
      snprintf(hex + 2 * k, 3, "%02x", digest[k]);
    if (ok && !strcmp(hex, hashes[i])) {
      font_encoded[i] = raw;
      font_lengths[i] = st.st_size;
    } else {
      free(raw);
      ESP_LOGW("fonts", "Fonte ausente/invalida: %s", paths[i]);
    }
  }
  storage_io_release();
  atomic_store(&font_state, 2);
  vTaskDelete(NULL);
}
static void fonts_ready(lv_timer_t *timer) {
  if (atomic_load(&font_state) != 2)
    return;
  atomic_store(&font_state, 3);
  lv_font_t *loaded[2] = {0};
  for (unsigned i = 0; i < 2; i++) {
    if (font_encoded[i] &&
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > font_lengths[i] + 1048576)
      loaded[i] =
          lv_binfont_create_from_buffer(font_encoded[i], font_lengths[i]);
    free(font_encoded[i]);
    font_encoded[i] = NULL;
  }
  if (loaded[0]) {
    loaded[0]->fallback = loaded[1];
    latin.fallback = loaded[0];
  } else
    latin.fallback = loaded[1];
  ESP_LOGI("fonts", "Fontes SD em memoria: CJK=%d emoji=%d", loaded[0] != NULL,
           loaded[1] != NULL);
  lv_obj_report_style_change(NULL);
  lv_timer_delete(timer);
}
#endif
static void load_unicode(void) {
  if (fonts_initialized)
    return;
  fonts_initialized = true;
  latin = ui_latin20;
#ifdef ESP_PLATFORM
  lv_timer_t *timer = lv_timer_create(fonts_ready, 100, NULL);
  if (!timer)
    return;
  if (xTaskCreate(font_worker, "font_loader", 4096, NULL, 2, NULL) != pdPASS) {
    lv_timer_delete(timer);
    atomic_store(&font_state, 3);
  }
#endif
}
static lv_font_t fonts[8];
static const lv_font_t *with_latin(const lv_font_t *f) {
  load_unicode();
  const lv_font_t *base[] = {&lv_font_montserrat_10, &lv_font_montserrat_14,
                             &lv_font_montserrat_16, &lv_font_montserrat_18,
                             &lv_font_montserrat_20, &lv_font_montserrat_22,
                             &lv_font_montserrat_24, &lv_font_montserrat_32};
  for (unsigned i = 0; i < sizeof(base) / sizeof(base[0]); i++)
    if (f == base[i]) {
      fonts[i] = *base[i];
      fonts[i].fallback = &latin;
      return &fonts[i];
    }
  return f;
}
static void menu_fonts(lv_obj_t *o, bool compact) {
  if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_USER_1))
    return;
  compact = compact || lv_obj_has_flag(o, LV_OBJ_FLAG_USER_4);
  const lv_font_t *f = lv_obj_get_style_text_font(o, 0);
  if (f && f->line_height < (compact ? 18 : 20))
    f = compact ? &lv_font_montserrat_18 : &lv_font_montserrat_20;
  if (f)
    lv_obj_set_style_text_font(o, with_latin(f), 0);
  for (unsigned i = 0; i < lv_obj_get_child_count(o); i++)
    menu_fonts(lv_obj_get_child(o, i), compact);
}
void ui_menu_fonts(lv_obj_t *o) {
  menu_fonts(o, false);
  menu_fonts(lv_layer_top(), false);
}
