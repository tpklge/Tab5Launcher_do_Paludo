#pragma once
#include "storage.h"
bool config_json_valid(const launcher_config_t *config);
/* Transactional: failure leaves config unchanged. Schema 1 and legacy objects. */
esp_err_t config_json_decode(const char *text, launcher_config_t *config);
/* Caller frees result. Complete config, no credentials or partition selection. */
char *config_json_encode(const launcher_config_t *config);
