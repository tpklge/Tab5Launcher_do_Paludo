#include "web_security.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void web_session_begin(web_session_t *s, const char token[WEB_TOKEN_LENGTH + 1],
                       int64_t now) {
  memset(s, 0, sizeof(*s));
  memcpy(s->token, token, WEB_TOKEN_LENGTH);
  s->expires = now + 15LL * 60 * 1000000;
}
bool web_session_accept(web_session_t *s, const char *token, size_t length,
                        int64_t now) {
  if (!s || !s->token[0] || now >= s->expires || now < s->blocked_until)
    return false;
  unsigned diff = 0;
  if (!token || length != WEB_TOKEN_LENGTH)
    diff = 1;
  else
    for (unsigned i = 0; i < WEB_TOKEN_LENGTH; i++)
      diff |= (unsigned char)token[i] ^ (unsigned char)s->token[i];
  if (diff) {
    if (++s->failures >= 8) {
      s->blocked_until = now + 30000000;
      s->failures = 0;
    }
    return false;
  }
  s->failures = 0;
  return true;
}
bool web_query_decode(const char *input, char *out, size_t size) {
  if (!input || !out || !size)
    return false;
  size_t n = 0;
  for (size_t i = 0; input[i]; i++) {
    unsigned char c = input[i];
    if (c == '%') {
      if (!input[i + 1] || !input[i + 2] ||
          !isxdigit((unsigned char)input[i + 1]) ||
          !isxdigit((unsigned char)input[i + 2]))
        return false;
      char pair[3] = {input[i + 1], input[i + 2], 0};
      c = strtoul(pair, NULL, 16);
      i += 2;
    } else if (c == '+')
      c = ' ';
    if (c < 32 || c == 127 || n + 1 >= size)
      return false;
    out[n++] = c;
  }
  out[n] = 0;
  return true;
}

bool web_path_is_private(const char *path) {
  if (!path)
    return true;
  const char *parts[] = {"sdcard", "launcher", "private"};
  const char *p = path;
  for (unsigned i = 0; i < 3; i++) {
    if (*p++ != '/')
      return false;
    const char *start = p;
    while (*p && *p != '/')
      p++;
    size_t n = p - start;
    while (n && (start[n - 1] == '.' || start[n - 1] == ' '))
      n--;
    if (n != strlen(parts[i]))
      return false;
    for (size_t j = 0; j < n; j++)
      if (tolower((unsigned char)start[j]) != parts[i][j])
        return false;
  }
  return true;
}

void web_logins_reset(web_login_t s[WEB_LOGIN_SESSIONS]) {
  memset(s, 0, sizeof(*s) * WEB_LOGIN_SESSIONS);
}
int web_login_issue(web_login_t s[WEB_LOGIN_SESSIONS], const char secret[33],
                    int64_t now) {
  int target = 0;
  for (int i = 0; i < WEB_LOGIN_SESSIONS; i++) {
    if (!s[i].secret[0]) {
      target = i;
      break;
    }
    if (s[i].last < s[target].last)
      target = i;
  }
  memcpy(s[target].secret, secret, 33);
  s[target].created = s[target].last = now;
  return target;
}
int web_login_accept(web_login_t s[WEB_LOGIN_SESSIONS], const char *cookie,
                     int64_t now) {
  if (!cookie || strlen(cookie) != 32)
    return -1;
  for (int i = 0; i < WEB_LOGIN_SESSIONS; i++) {
    if (!s[i].secret[0])
      continue;
    unsigned difference = 0;
    for (unsigned j = 0; j < 32; j++)
      difference |= (unsigned char)cookie[j] ^ (unsigned char)s[i].secret[j];
    if (!difference) {
      if (now - s[i].last > 2LL * 3600 * 1000000 ||
          now - s[i].created > 8LL * 3600 * 1000000) {
        memset(&s[i], 0, sizeof(s[i]));
        return -1;
      }
      s[i].last = now;
      return i;
    }
  }
  return -1;
}
bool web_cookie_extract(const char *header, char out[33]) {
  if (!header)
    return false;
  const char *p = header;
  while (*p) {
    while (*p == ' ' || *p == ';')
      p++;
    const char *end = strchr(p, ';');
    if (!end)
      end = p + strlen(p);
    if (end - p == 45 && !strncmp(p, "tab5_session=", 13)) {
      memcpy(out, p + 13, 32);
      out[32] = 0;
      for (unsigned i = 0; i < 32; i++)
        if (!isxdigit((unsigned char)out[i]))
          return false;
      return true;
    }
    p = end;
  }
  return false;
}
bool web_upload_path(const char *directory, const char *relative, char *out,
                     size_t size) {
  if (!directory || !relative || !*relative || *relative == '/' ||
      strlen(relative) > 400 || strchr(relative, '\\'))
    return false;
  const char *p = relative;
  unsigned depth = 0;
  while (*p) {
    const char *end = strchr(p, '/');
    if (!end)
      end = p + strlen(p);
    size_t n = end - p;
    if (!n || n > 255 || ++depth > 32 || (n == 1 && *p == '.') ||
        (n == 2 && !memcmp(p, "..", 2)) || p[n - 1] == '.' || p[n - 1] == ' ')
      return false;
    for (const char *q = p; q < end; q++)
      if ((unsigned char)*q < 32 || strchr("<>:\"|?*", *q))
        return false;
    p = *end ? end + 1 : end;
    if (*end && !*p)
      return false;
  }
  int n = snprintf(out, size, "%s/%s", directory, relative);
  return n > 0 && (size_t)n < size && !web_path_is_private(out);
}
