#include "esp_err.h"
esp_err_t esp_hosted_slave_ota_begin(void);
esp_err_t esp_hosted_slave_ota_write(unsigned char*,unsigned);
esp_err_t esp_hosted_slave_ota_end(void);
esp_err_t esp_hosted_slave_ota_activate(void);
