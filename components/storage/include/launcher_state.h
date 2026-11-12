#pragma once
/* Small transactional operational records in /launcher/private. No flash KV. */
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef struct launcher_state *launcher_state_handle_t;
enum { LAUNCHER_STATE_READONLY, LAUNCHER_STATE_READWRITE };
esp_err_t launcher_state_open(const char *, int, launcher_state_handle_t *);
void launcher_state_close(launcher_state_handle_t);
esp_err_t launcher_state_commit(launcher_state_handle_t);
esp_err_t launcher_state_erase_key(launcher_state_handle_t, const char *);
esp_err_t launcher_state_get_blob(launcher_state_handle_t, const char *, void *,
                                  size_t *);
esp_err_t launcher_state_set_blob(launcher_state_handle_t, const char *,
                                  const void *, size_t);
esp_err_t launcher_state_get_str(launcher_state_handle_t, const char *, char *,
                                 size_t *);
esp_err_t launcher_state_set_str(launcher_state_handle_t, const char *,
                                 const char *);
esp_err_t launcher_state_get_u8(launcher_state_handle_t, const char *,
                                uint8_t *);
esp_err_t launcher_state_set_u8(launcher_state_handle_t, const char *, uint8_t);
esp_err_t launcher_state_get_i8(launcher_state_handle_t, const char *,
                                int8_t *);
esp_err_t launcher_state_set_i8(launcher_state_handle_t, const char *, int8_t);
