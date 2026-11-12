#pragma once
/* input — Tab5 Keyboard A164 (I²C, addr 0x6D, SDA GPIO0, SCL GPIO1, INT GPIO50).
 *
 * The keyboard runs on its own I²C bus (port 0, 100 kHz), separate from the BSP system bus.
 * A worker task polls every 2 s for hot-plug and reads key events on the INT pin (GPIO50).
 * Keys are queued and consumed by the UI via input_get_key().
 *
 * Sources: UNIZERSION l5_kbd.h (da5aa984), bmorcelli Tab5 interface.cpp (2f43aa7b),
 *          M5Stack Tab5 Keyboard docs, M5Unit-KEYBOARD library.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pin assignments (confirmed from M5Stack Tab5 schematic and both reference launchers) */
#define INPUT_KBD_SDA    GPIO_NUM_0
#define INPUT_KBD_SCL    GPIO_NUM_1
#define INPUT_KBD_INT    GPIO_NUM_50
#define INPUT_KBD_ADDR   0x6D
#define INPUT_KBD_FREQ   100000  /* Hz */

typedef enum {
    KEY_KIND_CHAR = 1,
    KEY_KIND_ENTER,
    KEY_KIND_ESC,
    KEY_KIND_BACKSPACE,
    KEY_KIND_DELETE,
    KEY_KIND_TAB,
    KEY_KIND_LEFT,
    KEY_KIND_RIGHT,
    KEY_KIND_UP,
    KEY_KIND_DOWN,
    KEY_KIND_HOME,
    KEY_KIND_END,
    KEY_KIND_PGUP,
    KEY_KIND_PGDN,
    KEY_KIND_F1,  /* F1..F12: KEY_KIND_F(n) */
} key_kind_t;
#define KEY_KIND_F(n) (KEY_KIND_F1 + (n) - 1)

typedef struct {
    uint8_t  kind;    /* key_kind_t */
    char     ch;      /* KEY_KIND_CHAR: ASCII character */
    uint16_t uc;      /* KEY_KIND_CHAR: Unicode code point (0 → use ch) */
    bool     ctrl, shift, alt;
    bool     repeat;  /* auto-repeat while held */
} key_event_t;

/* Unicode code point for a KEY_KIND_CHAR event (handles uc/ch fallback) */
static inline uint32_t key_char(const key_event_t *k)
{
    if (k->kind != KEY_KIND_CHAR) return 0;
    return k->uc ? k->uc : (uint8_t)k->ch;
}

typedef enum {
    INPUT_KBD_STATE_ABSENT,     /* not detected / not connected */
    INPUT_KBD_STATE_PRESENT,    /* detected and in HID mode */
    INPUT_KBD_STATE_RECOVERING, /* recovery attempt in progress */
    INPUT_KBD_STATE_FAILED,     /* permanent failure until reinit */
} input_kbd_state_t;

/* Start the keyboard worker task (async, non-blocking, tolerates absent keyboard). */
void input_init(void);

/* Keyboard presence and state. */
bool              input_kbd_present(void);
input_kbd_state_t input_kbd_state(void);

/* Dequeue next key event; returns false when none pending. Safe from any task. */
bool input_get_key(key_event_t *out);

/* Recovery actions (exposed to Settings > Input screen) */
void input_kbd_detect(void);       /* trigger an immediate detection scan */
void input_kbd_reinit(void);       /* stop ISR/worker, free state, reinstall connection */
esp_err_t input_kbd_recover_bus(void); /* I²C bus recovery sequence (use with caution) */

#ifdef __cplusplus
}
#endif
