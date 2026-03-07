#pragma once
#include "../common/types.h"
#include "../common/menu.h"

/*
 * Passphrase input UI for SakuruBoot.
 * Renders a simple prompt on the current console, reads characters
 * with echo masked (shows '*' per character), and returns the result.
 *
 * buf     : caller-provided buffer to receive the null-terminated passphrase
 * buf_max : capacity of buf (including null terminator)
 * ops     : platform menu vtable for I/O
 * prompt  : label shown before the input field, e.g. "Passphrase: "
 *
 * Returns the number of characters entered (0 = empty / cancelled).
 */
u32 passphrase_read(u8 *buf, u32 buf_max,
                    const MenuOps *ops,
                    const char *prompt);

/* Wipe a passphrase buffer in memory (call after use) */
void passphrase_wipe(u8 *buf, u32 len);
