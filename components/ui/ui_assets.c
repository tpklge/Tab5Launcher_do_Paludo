#include "ui_assets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "libs/lodepng/lodepng.h"
#include "libs/tjpgd/tjpgd.h"
#include "sd_files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define LISTENERS 12
typedef struct {
  unsigned id, max_w, max_h, box_w, box_h;
  bool blur;
  char path[512];
} request_t;
typedef struct {
  unsigned id, w, h;
  uint8_t *pixels;
} result_t;
typedef struct {
  lv_obj_t *object;
  unsigned id, box_w, box_h;
  lv_image_dsc_t descriptor;
} listener_t;
static listener_t s_listeners[LISTENERS];
static QueueHandle_t s_jobs, s_results;
static unsigned s_next;
static bool s_ready;
static void free_image(listener_t *l) {
  if (l->descriptor.data) {
    lv_image_cache_drop(&l->descriptor);
    free((void *)l->descriptor.data);
    memset(&l->descriptor, 0, sizeof(l->descriptor));
  }
}
static void deleted(lv_event_t *e) {
  listener_t *l = lv_event_get_user_data(e);
  l->object = NULL;
  l->id = 0;
  free_image(l);
}
static uint32_t be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
         p[3];
}
typedef struct {
  FILE *file;
  uint8_t *pixels;
  unsigned width;
} jpeg_t;
static size_t jpeg_read(JDEC *j, uint8_t *out, size_t n) {
  jpeg_t *r = j->device;
  if (out)
    return fread(out, 1, n, r->file);
  return fseek(r->file, n, SEEK_CUR) == 0 ? n : 0;
}
static int jpeg_write(JDEC *j, void *pixels, JRECT *rect) {
  jpeg_t *r = j->device;
  uint8_t *p = pixels;
  for (unsigned y = rect->top; y <= rect->bottom; y++)
    for (unsigned x = rect->left; x <= rect->right; x++) {
      uint8_t *d = r->pixels + 4 * (y * r->width + x);
      d[0] = p[2];
      d[1] = p[1];
      d[2] = p[0];
      d[3] = 255;
      p += 3;
    }
  vTaskDelay(1);
  return 1;
}
static result_t decode_impl(const request_t *job) {
  result_t result = {.id = job->id};
  FILE *f = fopen(job->path, "rb");
  if (!f)
    return result;
  struct stat st;
  uint8_t head[24];
  if (fstat(fileno(f), &st) != 0 || st.st_size < 24 ||
      st.st_size > 4 * 1024 * 1024 || fread(head, 1, 24, f) != 24) {
    fclose(f);
    return result;
  }
  if (!memcmp(head, "\x89PNG\r\n\x1a\n", 8)) {
    unsigned w = be32(head + 16), h = be32(head + 20);
    if (w && h && w <= job->max_w && h <= job->max_h) {
      uint8_t *encoded = malloc(st.st_size), *rgba = NULL;
      unsigned out_w = 0, out_h = 0;
      if (encoded && fseek(f, 0, SEEK_SET) == 0 &&
          fread(encoded, 1, st.st_size, f) == st.st_size) {
        LodePNGState state;
        lodepng_state_init(&state);
        state.decoder.zlibsettings.max_output_size = (size_t)w * h * 8 + 65536;
        unsigned err =
            lodepng_decode(&rgba, &out_w, &out_h, &state, encoded, st.st_size);
        lodepng_state_cleanup(&state);
        if (!err && out_w == w && out_h == h) {
          result.pixels = malloc((size_t)w * h * 4);
          if (result.pixels) {
            for (size_t i = 0; i < (size_t)w * h; i++) {
              result.pixels[4 * i] = rgba[4 * i + 2];
              result.pixels[4 * i + 1] = rgba[4 * i + 1];
              result.pixels[4 * i + 2] = rgba[4 * i];
              result.pixels[4 * i + 3] = rgba[4 * i + 3];
            }
            result.w = w;
            result.h = h;
          }
        }
        if (rgba)
          lv_free(rgba);
      }
      free(encoded);
    }
  } else if (head[0] == 0xff && head[1] == 0xd8) {
    jpeg_t source = {.file = f};
    JDEC j;
    void *work = malloc(8192);
    fseek(f, 0, SEEK_SET);
    if (work && jd_prepare(&j, jpeg_read, work, 8192, &source) == JDR_OK &&
        j.width && j.height && j.width <= job->max_w &&
        j.height <= job->max_h) {
      source.width = j.width;
      source.pixels = malloc((size_t)j.width * j.height * 4);
      if (source.pixels && jd_decomp(&j, jpeg_write, 0) == JDR_OK) {
        result.pixels = source.pixels;
        result.w = j.width;
        result.h = j.height;
      } else
        free(source.pixels);
    }
    free(work);
  }
  fclose(f);
  return result;
}
static result_t decode(const request_t *job) {
  if (!sd_media_acquire(job->path))
    return (result_t){.id = job->id};
  result_t result = decode_impl(job);
  sd_media_release(job->path);
  return result;
}

static void blur(result_t *r) {
  if (!r->pixels)
    return;
  unsigned w = r->w, h = r->h;
  uint8_t *tmp = malloc((size_t)w * h * 4);
  if (!tmp)
    return;
  const int radius = 12;
  for (unsigned y = 0; y < h; y++) {
    unsigned sums[4] = {0};
    int left = 0, right = -1;
    for (unsigned x = 0; x < w; x++) {
      int end = (int)x + radius < (int)w ? (int)x + radius : (int)w - 1;
      while (right < end) {
        right++;
        for (int c = 0; c < 4; c++)
          sums[c] += r->pixels[4 * (y * w + right) + c];
      }
      int begin = (int)x - radius > 0 ? (int)x - radius : 0;
      while (left < begin) {
        for (int c = 0; c < 4; c++)
          sums[c] -= r->pixels[4 * (y * w + left) + c];
        left++;
      }
      for (int c = 0; c < 4; c++)
        tmp[4 * (y * w + x) + c] = sums[c] / (right - left + 1);
    }
    if (y % 32 == 0)
      vTaskDelay(1);
  }
  for (unsigned x = 0; x < w; x++) {
    unsigned sums[4] = {0};
    int top = 0, bottom = -1;
    for (unsigned y = 0; y < h; y++) {
      int end = (int)y + radius < (int)h ? (int)y + radius : (int)h - 1;
      while (bottom < end) {
        bottom++;
        for (int c = 0; c < 4; c++)
          sums[c] += tmp[4 * (bottom * w + x) + c];
      }
      int begin = (int)y - radius > 0 ? (int)y - radius : 0;
      while (top < begin) {
        for (int c = 0; c < 4; c++)
          sums[c] -= tmp[4 * (top * w + x) + c];
        top++;
      }
      for (int c = 0; c < 4; c++)
        r->pixels[4 * (y * w + x) + c] = sums[c] / (bottom - top + 1);
    }
    if (x % 32 == 0)
      vTaskDelay(1);
  }
  free(tmp);
}
static void worker(void *arg) {
  request_t job;
  while (1)
    if (xQueueReceive(s_jobs, &job, portMAX_DELAY) == pdTRUE) {
      result_t r = decode(&job);
      if (job.blur)
        blur(&r);
      if (xQueueSend(s_results, &r, portMAX_DELAY) != pdTRUE)
        free(r.pixels);
    }
}
static void poll(lv_timer_t *timer) {
  result_t r;
  while (xQueueReceive(s_results, &r, 0) == pdTRUE) {
    listener_t *found = NULL;
    for (unsigned i = 0; i < LISTENERS; i++)
      if (s_listeners[i].object && s_listeners[i].id == r.id) {
        found = &s_listeners[i];
        break;
      }
    if (!found || !r.pixels) {
      free(r.pixels);
      continue;
    }
    free_image(found);
    found->descriptor =
        (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                                    .cf = LV_COLOR_FORMAT_ARGB8888,
                                    .w = r.w,
                                    .h = r.h,
                                    .stride = r.w * 4},
                         .data_size = r.w * r.h * 4,
                         .data = r.pixels};
    lv_image_set_src(found->object, &found->descriptor);
    lv_obj_send_event(found->object, LV_EVENT_READY, NULL);
    unsigned scale = found->box_w * 256 / r.w, sy = found->box_h * 256 / r.h;
    if (sy < scale)
      scale = sy;
    lv_image_set_scale(found->object, scale ? scale : 1);
  }
}
void ui_assets_init(void) {
  if (s_jobs)
    return;
  s_jobs = xQueueCreate(8, sizeof(request_t));
  s_results = xQueueCreate(2, sizeof(result_t));
  if (s_jobs && s_results &&
      xTaskCreate(worker, "assets", 24576, NULL, 2, NULL) == pdPASS) {
    lv_timer_create(poll, 40, NULL);
    s_ready = true;
  }
}
static bool set_image(lv_obj_t *obj, const char *path, unsigned max_w,
                      unsigned max_h, unsigned box_w, unsigned box_h,
                      bool blur) {
  if (!s_ready || !s_jobs || !sd_path_valid(path) || max_w > 1280 ||
      max_h > 720 || !max_w || !max_h)
    return false;
  listener_t *l = NULL;
  for (unsigned i = 0; i < LISTENERS; i++)
    if (s_listeners[i].object == obj) {
      l = &s_listeners[i];
      break;
    }
  if (!l)
    for (unsigned i = 0; i < LISTENERS; i++)
      if (!s_listeners[i].object) {
        l = &s_listeners[i];
        l->object = obj;
        lv_obj_add_event_cb(obj, deleted, LV_EVENT_DELETE, l);
        break;
      }
  if (!l)
    return false;
  l->id = ++s_next;
  l->box_w = box_w;
  l->box_h = box_h;
  request_t job = {.id = l->id,
                   .max_w = max_w,
                   .max_h = max_h,
                   .box_w = box_w,
                   .box_h = box_h,
                   .blur = blur};
  snprintf(job.path, sizeof(job.path), "%s", path);
  return xQueueSend(s_jobs, &job, 0) == pdTRUE;
}

bool ui_assets_set(lv_obj_t *o, const char *p, unsigned mw, unsigned mh,
                   unsigned bw, unsigned bh) {
  return set_image(o, p, mw, mh, bw, bh, false);
}
bool ui_assets_set_blur(lv_obj_t *o, const char *p, unsigned mw, unsigned mh,
                        unsigned bw, unsigned bh) {
  return set_image(o, p, mw, mh, bw, bh, true);
}
