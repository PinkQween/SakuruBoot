/*
 * common/serial.h — Serial UART debug console (UEFI and BIOS builds)
 *
 * When the config contains `debug = yes`, all internal debug messages
 * are mirrored to the serial port.  On UEFI the EFI_SERIAL_IO_PROTOCOL
 * is used first; if unavailable, x86 falls back to direct port 0x3F8
 * (COM1).  AArch64 UEFI-only path uses SerialIo.
 *
 * All functions are no-ops when SAKURU_DEBUG is not defined (set by the
 * config engine when `debug = yes` is seen).
 *
 * Usage:
 *   serial_init();               // call once after config is loaded
 *   serial_putc('X');
 *   serial_puts("hello\n");
 *   serial_puthex64(value);
 */
#pragma once

#ifndef SAKURU_HOST_TEST
#include "types.h"

/* Initialise the serial port.  Must be called after gBS/gST are valid. */
void serial_init(void);

/* Send a single character */
void serial_putc(char c);

/* Send a null-terminated string */
void serial_puts(const char *s);

/* Print a 64-bit value as "0x%016llx\n" */
void serial_puthex64(u64 val);

/* Convenience macro — strips all overhead when debug is off */
#ifdef SAKURU_DEBUG
# define dbg(s)      serial_puts(s)
# define dbghex(v)   serial_puthex64((u64)(v))
#else
# define dbg(s)      ((void)0)
# define dbghex(v)   ((void)0)
#endif

#endif /* !SAKURU_HOST_TEST */
