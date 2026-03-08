/*
 * common/serial.c — Serial UART debug console
 */

#ifndef SAKURU_HOST_TEST

#include "serial.h"
#include "types.h"
#include "../uefi/efi.h"

extern EFI_SYSTEM_TABLE  *gST;
extern EFI_BOOT_SERVICES *gBS;

/* ── EFI Serial I/O Protocol ─────────────────────────────────────────── */
#ifndef EFI_SERIAL_IO_PROTOCOL_GUID
#define EFI_SERIAL_IO_PROTOCOL_GUID \
    EFI_GUID_INIT(0xbb25cf6f, 0xf1d4, 0x11d2, \
                  0x9a, 0x0c, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0xfd)
#endif

#define EFI_DEFAULT_BAUD_RATE   115200ULL
#define EFI_DEFAULT_DATA_BITS   8
#define EFI_DEFAULT_PARITY      1  /* NoParity */
#define EFI_DEFAULT_STOP_BITS   1  /* OneStopBit */

/* Minimal subset of EFI_SERIAL_IO_PROTOCOL we need */
typedef struct {
    u32  Revision;
    void *Reset;
    void *SetAttributes;
    void *SetControl;
    void *GetControl;
    /* Write(This, BufferSize*, Buffer) */
    EFI_STATUS (EFIAPI *Write)(void *This, UINTN *BufferSize, void *Buffer);
} EFI_SERIAL_IO;

static EFI_SERIAL_IO *s_serial = NULL;
#if defined(__x86_64__) || defined(__i386__)
static int             s_x86_fallback = 0;
#endif

/* ── x86 direct port I/O (COM1 at 0x3F8) ─────────────────────────────── */
#ifdef __x86_64__
static inline void outb(u16 port, u8 val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline u8 inb(u16 port) {
    u8 ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define COM1 0x3F8U
static void com1_init(void) {
    outb(COM1 + 1, 0x00); /* Disable interrupts */
    outb(COM1 + 3, 0x80); /* DLAB on            */
    outb(COM1 + 0, 0x01); /* Divisor = 1 → 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1, DLAB off      */
    outb(COM1 + 2, 0xC7); /* Enable FIFO, clear */
    outb(COM1 + 4, 0x0B); /* RTS + DTR + loopback off */
}
static void com1_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) {}
    outb(COM1, (u8)c);
}
#endif /* __x86_64__ */

/* ── EFI SerialIo path ────────────────────────────────────────────────── */
typedef EFI_STATUS (EFIAPI *LocateProtocolFn)(EFI_GUID *, void *, void **);
#define bs_locate_protocol ((LocateProtocolFn)(gBS->LocateProtocol))

void serial_init(void) {
    EFI_GUID guid = EFI_SERIAL_IO_PROTOCOL_GUID;
    EFI_STATUS s = bs_locate_protocol(&guid, NULL, (void **)&s_serial);
    if (s == 0 && s_serial) {
        /* Reset to defaults */
        void (EFIAPI *reset_fn)(void *) = s_serial->Reset;
        if (reset_fn) reset_fn(s_serial);
        return;
    }
    s_serial = NULL;

#ifdef __x86_64__
    /* Fall back to direct COM1 programming */
    com1_init();
    s_x86_fallback = 1;
#endif
}

void serial_putc(char c) {
    if (s_serial) {
        UINTN sz = 1;
        s_serial->Write(s_serial, &sz, &c);
        return;
    }
#ifdef __x86_64__
    if (s_x86_fallback) com1_putc(c);
#endif
}

void serial_puts(const char *s) {
    if (!s) return;
    if (s_serial) {
        UINTN len = 0;
        while (s[len]) len++;
        s_serial->Write(s_serial, &len, (void *)s);
        return;
    }
#ifdef __x86_64__
    if (s_x86_fallback) {
        while (*s) com1_putc(*s++);
    }
#endif
}

void serial_puthex64(u64 val) {
    char buf[19]; /* "0x" + 16 hex digits + '\n' */
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        u8 nibble = (u8)((val >> (60 - i * 4)) & 0xF);
        buf[2 + i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
    }
    buf[18] = '\n';
    if (s_serial) {
        UINTN sz = 19;
        s_serial->Write(s_serial, &sz, buf);
        return;
    }
#ifdef __x86_64__
    if (s_x86_fallback) {
        for (int i = 0; i < 19; i++) com1_putc(buf[i]);
    }
#endif
}

#endif /* !SAKURU_HOST_TEST */
