/* input.c — Tab5 Keyboard A164 driver.
 *
 * The keyboard is an STM32-based I²C HID device. After power-on it may be in Normal,
 * Character, or HID mode depending on what the previous app left it in.
 * We configure it to HID mode: one event per key (modifier byte + HID usage ID).
 *
 * Register map follows M5Stack M5Tab5-Keyboard-Internal-FW user_i2c_reg.h.
 * Worker is the sole owner of I2C device configuration and recovery.
 */
#include "input.h"
#include <string.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "input";

#define KBD_REG_INT_CFG   0x00
#define KBD_REG_INT_STA   0x01
#define KBD_REG_STATUS    0x02
#define KBD_REG_VALUE     0x30
#define KBD_REG_MODE      0x10
#define KBD_MODE_HID      0x01
#define KBD_STATUS_PRESSED 0x80

#define KEY_QUEUE_SIZE    32
#define WORKER_STACK      3072
#define HOTPLUG_SCAN_MS   20  /* periodic scan for hot-plug */
#define REPEAT_FIRST_MS   500
#define REPEAT_INTERVAL_MS 80

static i2c_master_bus_handle_t s_bus  = NULL;
static i2c_master_dev_handle_t s_dev  = NULL;
static input_kbd_state_t       s_state = INPUT_KBD_STATE_ABSENT;
static QueueHandle_t           s_queue = NULL;
static TaskHandle_t            s_worker = NULL;
static volatile bool           s_int_fired = false;
static volatile bool s_reconfigure;
static volatile bool s_recover_bus;

extern bool input_hid_to_key(uint8_t mod, uint8_t usage, key_event_t *out);

/* ---- ISR ----------------------------------------------------------------- */

static void IRAM_ATTR kbd_int_isr(void *arg)
{
    s_int_fired = true;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_worker, &woken);
    portYIELD_FROM_ISR(woken);
}

/* ---- I²C helpers --------------------------------------------------------- */

static esp_err_t kbd_bus_init(void)
{
    if (s_bus) return ESP_OK;
    i2c_master_bus_config_t cfg = {
        .i2c_port     = I2C_NUM_0,
        .sda_io_num   = INPUT_KBD_SDA,
        .scl_io_num   = INPUT_KBD_SCL,
        .clk_source   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_bus);
}

static esp_err_t kbd_dev_init(void)
{
    if (s_dev) return ESP_OK;
    if (!s_bus) return ESP_ERR_INVALID_STATE;
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = INPUT_KBD_ADDR,
        .scl_speed_hz    = INPUT_KBD_FREQ,
    };
    return i2c_master_bus_add_device(s_bus, &dev, &s_dev);
}

static esp_err_t kbd_write_reg(uint8_t reg, uint8_t val)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, 2, 50);
}

static esp_err_t kbd_read_reg(uint8_t reg, uint8_t *out, size_t len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, 50);
}

/* ---- Key mapping --------------------------------------------------------- */

/* ---- Detection & mode setup ---------------------------------------------- */

static bool kbd_detect_and_configure(void)
{
    if (kbd_dev_init() != ESP_OK) return false;

    uint8_t address = 0;
    if (kbd_read_reg(0xFF, &address, 1) != ESP_OK || address != INPUT_KBD_ADDR ||
        kbd_write_reg(KBD_REG_MODE, KBD_MODE_HID) != ESP_OK ||
        kbd_write_reg(KBD_REG_INT_CFG, 0x02) != ESP_OK ||
        kbd_write_reg(KBD_REG_STATUS, 0) != ESP_OK ||
        kbd_write_reg(KBD_REG_INT_STA, 0) != ESP_OK) {
        if (s_dev) { i2c_master_bus_rm_device(s_dev); s_dev = NULL; }
        return false;
    }

    ESP_LOGI(TAG, "keyboard detected and set to HID mode");
    return true;
}

/* ---- Worker task --------------------------------------------------------- */

static void kbd_worker(void *arg)
{
    uint32_t poll_ticks = pdMS_TO_TICKS(HOTPLUG_SCAN_MS);

    /* INT pin: falling edge triggers our ISR */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << INPUT_KBD_INT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io);
    esp_err_t isr_err = gpio_isr_handler_add(INPUT_KBD_INT, kbd_int_isr, NULL);
    if (isr_err == ESP_ERR_INVALID_STATE) {
        if (gpio_install_isr_service(0) == ESP_OK)
            gpio_isr_handler_add(INPUT_KBD_INT, kbd_int_isr, NULL);
    }

    bool kbd_ok = kbd_detect_and_configure();
    s_state = kbd_ok ? INPUT_KBD_STATE_PRESENT : INPUT_KBD_STATE_ABSENT;

    while (1) {
        /* Wait for INT notification or poll timeout */
        uint32_t notif = ulTaskNotifyTake(pdTRUE, poll_ticks);

        if (s_reconfigure) {
            s_reconfigure = false;
            kbd_ok = false;
            if (s_dev) { i2c_master_bus_rm_device(s_dev); s_dev = NULL; }
            if (s_recover_bus) { s_recover_bus = false; i2c_master_bus_reset(s_bus); }
            xQueueReset(s_queue);
        }
        if (!kbd_ok) {
            /* Try to detect keyboard */
            kbd_ok = kbd_detect_and_configure();
            s_state = kbd_ok ? INPUT_KBD_STATE_PRESENT : INPUT_KBD_STATE_ABSENT;
            if (!kbd_ok) continue;
        }

        /* Read all pending key events */
        for (int attempts = 0; attempts < 32; attempts++) {
            uint8_t status = 0;
            if (kbd_read_reg(KBD_REG_STATUS, &status, 1) != ESP_OK) {
                ESP_LOGW(TAG, "I²C read error — keyboard may have disconnected");
                kbd_ok  = false;
                s_state = INPUT_KBD_STATE_ABSENT;
                if (s_dev) { i2c_master_bus_rm_device(s_dev); s_dev = NULL; }
                break;
            }
            if (status == 0) break;

            uint8_t val[2] = {0, 0};
            if (kbd_read_reg(KBD_REG_VALUE, val, 2) != ESP_OK) break;

            uint8_t mod   = val[0];
            uint8_t usage = val[1];
            if (usage == 0 || (mod == 0xFF && usage == 0xFF)) continue;

            key_event_t ev;
            if (input_hid_to_key(mod, usage, &ev)) {
                xQueueSend(s_queue, &ev, 0);  /* drop if full */
            }
        }

        if (kbd_ok) kbd_write_reg(KBD_REG_INT_STA, 0);
        if (!kbd_ok) vTaskDelay(pdMS_TO_TICKS(1000));
        /* Re-arm INT if it fired while we were reading */
        s_int_fired = false;
    }
}

/* ---- Public API ---------------------------------------------------------- */

void input_init(void)
{
    if (s_worker) return;
    s_queue = xQueueCreate(KEY_QUEUE_SIZE, sizeof(key_event_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "queue alloc failed");
        return;
    }

    if (kbd_bus_init() != ESP_OK) {
        ESP_LOGW(TAG, "keyboard I²C bus init failed — keyboard not available");
        return;
    }

    xTaskCreate(kbd_worker, "kbd_worker", WORKER_STACK, NULL, 5, &s_worker);
    ESP_LOGI(TAG, "keyboard worker started");
}

bool input_kbd_present(void)          { return s_state == INPUT_KBD_STATE_PRESENT; }
input_kbd_state_t input_kbd_state(void) { return s_state; }

bool input_get_key(key_event_t *out)
{
    if (!s_queue) return false;
    return xQueueReceive(s_queue, out, 0) == pdTRUE;
}

void input_kbd_detect(void)
{
    if (s_worker) xTaskNotifyGive(s_worker);
}

void input_kbd_reinit(void)
{
    s_reconfigure = true;
    if (s_worker) xTaskNotifyGive(s_worker);
}

esp_err_t input_kbd_recover_bus(void)
{
    if (!s_worker) return ESP_ERR_INVALID_STATE;
    s_recover_bus = true;
    input_kbd_reinit();
    return ESP_OK; /* request queued; presence reports the eventual result */
}
