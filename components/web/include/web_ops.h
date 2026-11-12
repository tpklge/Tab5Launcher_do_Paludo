#pragma once
#include "cJSON.h"
#include <stdbool.h>
bool web_ops_busy(void);
cJSON *web_ops_request(const cJSON *);
cJSON *web_ops_status(void);

void web_ops_remote_init(void);
