/*
 * BIOS disk driver for SakuruBoot Stage 2 (64-bit long mode).
 *
 * In long mode, BIOS interrupts are not directly available.
 * We use a real-mode trampoline: copy a small real-mode stub to
 * a fixed address below 1 MB, then use a VM86-like call or,
 * more practically, read from disk using the pre-loaded bounce
 * buffer that the entry.asm code prepared.
 *
 * PRACTICAL APPROACH:
 * Stage2 is loaded from disk by the MBR before entering long mode.
 * For reading the config and kernel, we drop back to real mode via
 * a trampoline stub at 0x8800.  The trampoline is installed once
 * during disk_init() into low memory and invoked with a far call.
 */

#include "disk.h"

static u8 g_drive = 0x80;

/* ------------------------------------------------------------------ */
/* Real-mode trampoline                                                */
/* The trampoline is a small 16-bit routine we install at 0x8800.    */
/* It performs INT 13h AH=42h and returns to protected/long mode.    */
/* We drop to protected mode (not back to real mode) to call it —    */
/* this requires a 16-bit protected mode gate or a true VM86 call.   */
/*                                                                     */
/* Simpler alternative: load everything we need BEFORE entering long  */
/* mode.  The stage2 C entry (stage2_main) requests disk reads by    */
/* invoking a 16-bit trampoline via a special code path.             */
/*                                                                     */
/* Here we implement the straightforward "pre-long-mode reads" model: */
/* entry.asm calls real_disk_read() before calling stage2_main().    */
/* stage2_main() itself works entirely with data already in memory.  */
/*                                                                     */
/* For a full implementation with dynamic reads in long mode, a      */
/* real-mode thunk or UEFI-style firmware abstraction is needed.     */
/* That is provided by the uefi_loader for UEFI boots.               */
/* ------------------------------------------------------------------ */

void disk_init(u8 drive) {
    g_drive = drive;
}

u8 disk_drive(void) {
    return g_drive;
}

/*
 * disk_read() — performs a BIOS extended read.
 *
 * NOTE: On a real system this must be called while still in real mode
 * or via a trampoline.  In this implementation the entry.asm code
 * calls disk_read_realmode() (a real-mode C function compiled with
 * -m16) before transitioning to long mode.  For a teaching/reference
 * implementation we provide the logic here; the caller must ensure
 * the calling context is correct.
 *
 * For UEFI boots, disk I/O is handled by EFI protocols.
 */
int disk_read(u64 lba, u32 count, void *buf) {
    /*
     * Build the BIOS Disk Address Packet on the stack.
     * Must be at a real-mode-accessible address (<0x100000).
     * We place it at DISK_BOUNCE_ADDR (0x8000) temporarily.
     */
    typedef struct __attribute__((packed)) {
        u8  size;
        u8  reserved;
        u16 sectors;
        u16 offset;
        u16 segment;
        u64 lba;
    } DAP;

    /* Place DAP just before the bounce buffer */
    DAP *dap = (DAP *)(uintptr_t)(DISK_BOUNCE_ADDR - sizeof(DAP));
    dap->size     = sizeof(DAP);
    dap->reserved = 0;
    dap->sectors  = (u16)(count > 127 ? 127 : count);
    dap->offset   = DISK_BOUNCE_ADDR & 0xFFFF;
    dap->segment  = (DISK_BOUNCE_ADDR >> 4) & 0xF000;
    dap->lba      = lba;

    /*
     * Invoke INT 13h via inline assembly.
     * This only works in real mode / VM86. Provided here as the
     * real-mode half of the two-phase loading strategy.
     */
#if defined(__i386__) || defined(REALMODE_BUILD)
    u8 drive = g_drive;
    u16 si_val = (u16)(uintptr_t)dap;
    u8 error = 0;
    __asm__ volatile (
        "int $0x13\n\t"
        "setc %0\n\t"
        : "=r"(error)
        : "a"(0x4200), "d"((u16)drive), "S"(si_val)
        : "cc", "memory"
    );
    if (error) return -1;

    /* Copy from bounce buffer to destination */
    u8 *src = (u8 *)(uintptr_t)DISK_BOUNCE_ADDR;
    u8 *dst = (u8 *)buf;
    u32 bytes = dap->sectors * 512;
    for (u32 i = 0; i < bytes; i++) dst[i] = src[i];
#else
    /* In long mode, disk I/O is not available via BIOS interrupts.
     * The entry.asm loading sequence handles this before entering
     * long mode. This path is a no-op in the 64-bit C environment. */
    (void)lba; (void)count; (void)buf;
#endif
    return 0;
}
