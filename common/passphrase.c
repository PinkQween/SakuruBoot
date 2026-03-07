#include "passphrase.h"

#define KEY_BACKSPACE 0x08
#define KEY_ENTER     '\r'
#define KEY_ESC       0x1B

u32 passphrase_read(u8 *buf, u32 buf_max, const MenuOps *ops, const char *prompt) {
    if (ops->show_cursor) ops->show_cursor(1);

    ops->print(prompt);

    u32 len = 0;
    for (;;) {
        int k = ops->read_key();
        if (k < 0) continue;

        if (k == KEY_ENTER) {
            ops->print("\r\n");
            break;
        }
        if (k == KEY_ESC) {
            /* Cancel — wipe and return 0 */
            passphrase_wipe(buf, buf_max);
            ops->print("\r\n");
            if (ops->show_cursor) ops->show_cursor(0);
            return 0;
        }
        if (k == KEY_BACKSPACE || k == 0x7F) {
            if (len > 0) {
                len--;
                buf[len] = 0;
                /* Erase the last '*' on screen */
                ops->print("\b \b");
            }
            continue;
        }
        if (k >= 0x20 && k <= 0x7E && len + 1 < buf_max) {
            buf[len++] = (u8)k;
            buf[len]   = 0;
            ops->print("*");
        }
    }

    if (ops->show_cursor) ops->show_cursor(0);
    return len;
}

void passphrase_wipe(u8 *buf, u32 len) {
    volatile u8 *p = buf;
    while (len--) *p++ = 0;
}
