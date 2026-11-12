#include "storage_guard.h"
#include "board.h"
#include "diagnostics.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
static void little32(uint8_t *p, uint32_t v) {
  for (unsigned n = 0; n < 4; n++)
    p[n] = (uint8_t)(v >> (n * 8));
}
esp_err_t diagnostics_capture(const char *path) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!path || !board_sd_ready())
    return ESP_ERR_INVALID_STATE;
  if (!board_lvgl_lock(2000))
    return ESP_ERR_TIMEOUT;
  lv_draw_buf_t *image =
      lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
  board_lvgl_unlock();
  if (!image)
    return ESP_ERR_NO_MEM;
  uint32_t width = image->header.w, height = image->header.h;
  esp_err_t result = ESP_OK;
  if (!width || width > 1280 || !height || height > 720 ||
      image->header.stride < width * 4)
    result = ESP_ERR_INVALID_SIZE;
  FILE *file = result == ESP_OK ? fopen(path, "wx") : NULL;
  if (!file && result == ESP_OK)
    result = ESP_FAIL;
  if (file) {
    uint8_t header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    little32(header + 2, 54 + width * height * 4);
    little32(header + 10, 54);
    little32(header + 14, 40);
    little32(header + 18, width);
    little32(header + 22, (uint32_t)(-(int32_t)height));
    header[26] = 1;
    header[28] = 32;
    little32(header + 34, width * height * 4);
    if (fwrite(header, 1, sizeof(header), file) != sizeof(header))
      result = ESP_FAIL;
    for (unsigned row = 0; row < height && result == ESP_OK; row++)
      if (fwrite(image->data + row * image->header.stride, 1, width * 4,
                 file) != width * 4)
        result = ESP_FAIL;
    if (fflush(file) || fsync(fileno(file)))
      result = ESP_FAIL;
    if (fclose(file))
      result = ESP_FAIL;
    if (result != ESP_OK)
      remove(path);
  }
  if (board_lvgl_lock(0)) {
    lv_draw_buf_destroy(image);
    board_lvgl_unlock();
  }
  return result;
}
