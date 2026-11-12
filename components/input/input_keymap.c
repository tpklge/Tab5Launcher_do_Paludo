/* HID usage mapping; shared by the driver and host regressions. */
#include "input.h"
#include <string.h>
/* HID modifier bits → struct fields */
#define MOD_LCTRL  (1 << 0)
#define MOD_LSHIFT (1 << 1)
#define MOD_LALT   (1 << 2)
#define MOD_RCTRL  (1 << 4)
#define MOD_RSHIFT (1 << 5)
#define MOD_RALT   (1 << 6)

/* HID usage IDs for non-printable keys */
#define HID_ENTER     0x28
#define HID_ESC       0x29
#define HID_BACKSPACE 0x2A
#define HID_TAB       0x2B
#define HID_RIGHT     0x4F
#define HID_LEFT      0x50
#define HID_DOWN      0x51
#define HID_UP        0x52
#define HID_DELETE    0x4C
#define HID_HOME      0x4A
#define HID_END       0x4D
#define HID_PGUP      0x4B
#define HID_PGDN      0x4E

/* US HID usage → ASCII (shifted and unshifted), usage IDs 0x04 (a) .. 0x27 (0) */
static const char s_hid_unshift[36] = "abcdefghijklmnopqrstuvwxyz1234567890";
static const char s_hid_shift[36]   = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()";

bool input_hid_to_key(uint8_t mod, uint8_t usage, key_event_t *out)
{
    memset(out, 0, sizeof(*out));
    out->ctrl  = !!(mod & (MOD_LCTRL  | MOD_RCTRL));
    out->shift = !!(mod & (MOD_LSHIFT | MOD_RSHIFT));
    out->alt   = !!(mod & (MOD_LALT   | MOD_RALT));

    if (usage >= 0x04 && usage <= 0x27) {
        int idx = usage - 0x04;
        out->kind = KEY_KIND_CHAR;
        out->ch   = out->shift ? s_hid_shift[idx] : s_hid_unshift[idx];
        out->uc   = 0;  /* ASCII range: uc = 0, ch is used */
        return true;
    }
    if (usage >= 0x2C && usage <= 0x38) {
        static const char plain[] = " -=[]\\#;'`,./";
        static const char shifted[] = " _+{}|~:\"~<>?";
        out->kind = KEY_KIND_CHAR;
        out->ch = out->shift ? shifted[usage - 0x2C] : plain[usage - 0x2C];
        return true;
    }
    switch (usage) {
    case HID_ENTER:     out->kind = KEY_KIND_ENTER;     return true;
    case HID_ESC:       out->kind = KEY_KIND_ESC;       return true;
    case HID_BACKSPACE: out->kind = KEY_KIND_BACKSPACE;  return true;
    case HID_TAB:       out->kind = KEY_KIND_TAB;        return true;
    case HID_RIGHT:     out->kind = KEY_KIND_RIGHT;      return true;
    case HID_LEFT:      out->kind = KEY_KIND_LEFT;       return true;
    case HID_DOWN:      out->kind = KEY_KIND_DOWN;       return true;
    case HID_UP:        out->kind = KEY_KIND_UP;         return true;
    case HID_DELETE:    out->kind = KEY_KIND_DELETE;     return true;
    case HID_HOME:      out->kind = KEY_KIND_HOME;       return true;
    case HID_END:       out->kind = KEY_KIND_END;        return true;
    case HID_PGUP:      out->kind = KEY_KIND_PGUP;       return true;
    case HID_PGDN:      out->kind = KEY_KIND_PGDN;       return true;
    default:
        if (usage >= 0x3A && usage <= 0x45) {  /* F1..F12 */
            out->kind = (uint8_t)KEY_KIND_F(usage - 0x3A + 1);
            return true;
        }
        return false;
    }
}

