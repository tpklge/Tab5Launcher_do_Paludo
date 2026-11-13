#include "layout_model.h"
#include <string.h>
static uint32_t u32(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t n) {
  for (int i = 0; i < 4; i++)
    p[i] = n >> (8 * i);
}
bool layout_build_big(const uint8_t old[4096], uint8_t out[4096],
                      layout_md5_fn md5) {
  int app0 = -1, app1 = -1, count = 0;
  uint8_t sum[16];
  bool checked = false;
  for (unsigned off = 0; off < 3072; off += 32) {
    const uint8_t *e = old + off;
    if (e[0] == 0xeb && e[1] == 0xeb) {
      checked = md5(old, off, sum) && !memcmp(sum, e + 16, 16);
      break;
    }
    if (e[0] != 0xaa || e[1] != 0x50)
      return false;
    uint32_t a = u32(e + 4), n = u32(e + 8);
    if ((a < 0x15000 && a + n > 0x13000) || !n || a < 0x9000 || a > 0x1000000 ||
        n > 0x1000000 - a || (a & 4095) || (n & 4095))
      return false;
    if (e[2] == 0 && (a & 65535))
      return false;
    for (int j = 0; j < count; j++) {
      uint32_t b = u32(old + j * 32 + 4), m = u32(old + j * 32 + 8);
      if (a < b + m && b < a + n)
        return false;
    }
    if (e[2] == 0 && e[3] == 16 && a == 0x210000 && n == 0x400000 &&
        !memcmp(e + 12, "app0\0", 5))
      app0 = count;
    if (e[2] == 0 && e[3] == 17 && a == 0x610000 && n == 0x400000 &&
        !memcmp(e + 12, "app1\0", 5))
      app1 = count;
    count++;
  }
  if (!checked || app0 < 0 || app1 < 0 || app0 == app1)
    return false;
  memset(out, 255, 4096);
  unsigned at = 0;
  for (int i = 0; i < count; i++)
    if (i != app1) {
      memcpy(out + at, old + 32 * i, 32);
      if (i == app0)
        put32(out + at + 8, 0x800000);
      at += 32;
    }
  out[at] = out[at + 1] = 0xeb;
  if (!md5(out, at, out + at + 16))
    return false;
  return true;
}
bool layout_build_profile(const uint8_t old[4096], uint8_t out[4096],
                          const uint32_t *sizes, unsigned count,
                          layout_md5_fn md5) {
  if (!old || !out || !sizes || !md5 || count < 1 || count > 4)
    return false;
  uint32_t total = 0;
  for (unsigned i = 0; i < count; i++) {
    if (sizes[i] < 0x100000 || (sizes[i] & 65535) ||
        sizes[i] > 0x800000 - total)
      return false;
    total += sizes[i];
  }
  if (total != 0x800000)
    return false;
  unsigned entries = 0, bank = 0;
  uint32_t bank_size = 0;
  bool checked = false;
  uint8_t digest[16];
  for (unsigned off = 0; off < 3072; off += 32) {
    const uint8_t *e = old + off;
    if (e[0] == 0xeb && e[1] == 0xeb) {
      checked = md5(old, off, digest) && !memcmp(digest, e + 16, 16);
      break;
    }
    if (e[0] != 0xaa || e[1] != 0x50)
      return false;
    uint32_t a = u32(e + 4), n = u32(e + 8);
    if (!n || a < 0x9000 || a > 0x1000000 || n > 0x1000000 - a || (a & 4095) ||
        (n & 4095) || (a < 0x15000 && a + n > 0x13000) ||
        (e[2] == 0 && (a & 65535)))
      return false;
    for (unsigned j = 0; j < entries; j++) {
      uint32_t b = u32(old + j * 32 + 4), m = u32(old + j * 32 + 8);
      if (a < b + m && b < a + n)
        return false;
    }
    if (a < 0xa10000 && a + n > 0x210000) {
      if (e[2] != 0 || e[3] < 16 || e[3] > 31 || a < 0x210000 ||
          a + n > 0xa10000 || u32(e + 28))
        return false;
      bank++;
      bank_size += n;
    }
    entries++;
  }
  if (!checked || !bank || bank_size != 0x800000 ||
      entries - bank + count >= 95)
    return false;
  memset(out, 255, 4096);
  unsigned at = 0;
  bool inserted = false;
  for (unsigned i = 0; i < entries; i++) {
    const uint8_t *e = old + i * 32;
    uint32_t a = u32(e + 4);
    if (a >= 0x210000 && a < 0xa10000) {
      if (!inserted) {
        uint32_t address = 0x210000;
        for (unsigned j = 0; j < count; j++) {
          uint8_t *p = out + at;
          memset(p, 0, 32);
          p[0] = 0xaa;
          p[1] = 0x50;
          p[3] = 16 + j;
          put32(p + 4, address);
          put32(p + 8, sizes[j]);
          memcpy(p + 12, "app0", 4);
          p[15] = '0' + j;
          address += sizes[j];
          at += 32;
        }
        inserted = true;
      }
    } else {
      memcpy(out + at, e, 32);
      at += 32;
    }
  }
  out[at] = out[at + 1] = 0xeb;
  return md5(out, at, out + at + 16);
}
static bool assign_next(const uint32_t *u, unsigned n, const uint32_t *s,
                        unsigned count, int *out, unsigned at, unsigned mask) {
  if (at == n)
    return true;
  if (!u[at]) {
    out[at] = -1;
    return assign_next(u, n, s, count, out, at + 1, mask);
  }
  for (unsigned k = 0; k < count; k++) {
    unsigned j = (at < count) ? (k == 0 ? at : (k <= at ? k - 1 : k)) : k;
    if ((mask & (1u << j)) || u[at] > s[j])
      continue;
    out[at] = j;
    if (assign_next(u, n, s, count, out, at + 1, mask | (1u << j)))
      return true;
  }
  return false;
}
bool layout_assign_apps(const uint32_t *u, unsigned n, const uint32_t *s,
                        unsigned count, int *out) {
  if (!u || !s || !out || n > 8 || count > 4 || !count)
    return false;
  return assign_next(u, n, s, count, out, 0, 0);
}
uint32_t layout_factory_size(const uint8_t table[4096]) {
  for (unsigned at = 0; at < 3072 && table[at] == 0xaa; at += 32)
    if (!table[at + 2] && !table[at + 3] && u32(table + at + 4) == 0x20000)
      return u32(table + at + 8);
  return 0;
}
uint32_t layout_app_end(const uint8_t table[4096]) {
  uint32_t end = 0xff0000u; /* preserve final coredump sector range */
  for (unsigned at = 0; at < 3072 && table[at] == 0xaa; at += 32) {
    const uint8_t *e = table + at;
    uint32_t address = u32(e + 4);
    if (e[2] != 0 && address >= 0x20000 && address < end)
      end = address;
  }
  return end & ~65535u;
}
bool layout_build_compact(const uint8_t old[4096], uint8_t out[4096],
                          uint32_t launcher_size,
                          const uint32_t sizes[LAYOUT_APP_LIMIT],
                          layout_md5_fn md5) {
  uint32_t bank_end = old ? layout_app_end(old) : 0;
  if (bank_end < 0x30000 || !old || !out || !sizes || !md5 || !launcher_size ||
      (launcher_size & 65535) || launcher_size > bank_end - 0x30000)
    return false;
  unsigned count = 0, factory = 0;
  bool checked = false;
  uint8_t digest[16];
  uint32_t old_factory = layout_factory_size(old);
  if (!old_factory)
    return false;
  for (unsigned at = 0; at < 3072; at += 32) {
    const uint8_t *e = old + at;
    if (e[0] == 0xeb && e[1] == 0xeb) {
      checked = md5(old, at, digest) && !memcmp(digest, e + 16, 16);
      break;
    }
    if (e[0] != 0xaa || e[1] != 0x50)
      return false;
    uint32_t a = u32(e + 4), n = u32(e + 8);
    if (!n || a < 0x9000 || a >= 0x1000000 || n > 0x1000000 - a || (a & 4095) ||
        (n & 4095) || (a < 0x16000 && a + n > 0x13000) ||
        (e[2] == 0 && (a & 65535)))
      return false;
    for (unsigned j = 0; j < count; j++) {
      uint32_t b = u32(old + j * 32 + 4), m = u32(old + j * 32 + 8);
      if (a < b + m && b < a + n)
        return false;
    }
    if (!e[2] && !e[3]) {
      if (a != 0x20000 || u32(e + 28) || ++factory != 1)
        return false;
    } else if (!e[2] && e[3] >= 16 && e[3] < 32) {
      if (a < 0x20000 + old_factory || a + n > bank_end || u32(e + 28))
        return false;
    } else if (a >= 0x20000 && a < bank_end)
      return false;
    count++;
  }
  if (!checked || factory != 1)
    return false;
  uint32_t total = 0;
  for (unsigned i = 0; i < LAYOUT_APP_LIMIT; i++) {
    if ((sizes[i] & 65535) ||
        sizes[i] > bank_end - 0x20000 - launcher_size - total)
      return false;
    total += sizes[i];
  }
  memset(out, 255, 4096);
  unsigned at = 0;
  for (unsigned i = 0; i < count; i++) {
    const uint8_t *e = old + i * 32;
    if (!e[2] && e[3] >= 16 && e[3] < 32)
      continue;
    memcpy(out + at, e, 32);
    if (!e[2] && !e[3])
      put32(out + at + 8, launcher_size);
    at += 32;
  }
  uint32_t address = 0x20000 + launcher_size;
  for (unsigned i = 0; i < LAYOUT_APP_LIMIT; i++)
    if (sizes[i]) {
      uint8_t *e = out + at;
      memset(e, 0, 32);
      e[0] = 0xaa;
      e[1] = 0x50;
      e[3] = 16 + i;
      put32(e + 4, address);
      put32(e + 8, sizes[i]);
      if (i < 10) {
        memcpy(e + 12, "app0", 5);
        e[15] = '0' + i;
      } else {
        memcpy(e + 12, "app10", 6);
        e[16] = '0' + i - 10;
      }
      address += sizes[i];
      at += 32;
    }
  if (at >= 3040)
    return false;
  out[at] = out[at + 1] = 0xeb;
  return md5(out, at, out + at + 16);
}
