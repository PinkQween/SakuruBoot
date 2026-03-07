#pragma once

#include "../../common/types.h"

/*
 * BIOS disk I/O via INT 13h Extended Read (AH=42h).
 * All functions call real-mode BIOS through a trampoline
 * set up by entry.asm before we entered long mode.
 *
 * For long mode, we use a simple software trampoline that temporarily
 * drops to real mode. Here we expose a simpler API: read LBA sectors
 * into a buffer below 1 MB (accessible from real mode).
 *
 * disk_init()  — must be called first, sets the drive number.
 * disk_read()  — read count sectors from lba into buf.
 *                buf must be a physical address < 1 MB.
 */

/* Real-mode disk bounce buffer sits at 0x00008000 (32 KB) */
#define DISK_BOUNCE_ADDR  0x00008000UL
#define DISK_BOUNCE_SIZE  (32 * 1024)

void   disk_init(u8 drive);
int    disk_read(u64 lba, u32 count, void *buf);

/* Returns drive number saved by entry.asm */
u8     disk_drive(void);
