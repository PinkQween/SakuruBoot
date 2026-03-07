#pragma once

#include "../common/types.h"
#include "../common/config.h"

/*
 * OS Loader Interface — the modular kernel loading API.
 *
 * Each supported OS/kernel type provides one OSLoader struct.
 * To add support for a new OS:
 *   1. Create a new file in os/ (e.g., my_os.c)
 *   2. Implement the three function pointers below
 *   3. Declare `extern const OSLoader my_os_loader;`
 *   4. Add it to the loader_registry[] in uefi_loader.c / stage2 main.c
 */

/*
 * Platform-neutral boot information structure.
 * Populated by the bootloader before calling loader->boot().
 * The kernel uses this to discover hardware resources.
 */
typedef struct {
    /* Memory map (UEFI: EFI_MEMORY_DESCRIPTOR array; BIOS: E820 entries) */
    u64  mem_map_addr;
    u64  mem_map_size;
    u64  mem_map_entry_size;
    u32  mem_map_version;

    /* Framebuffer */
    u64  fb_addr;
    u32  fb_width;
    u32  fb_height;
    u32  fb_pitch;
    u32  fb_bpp;
    u32  fb_pixel_format; /* 0=RGB, 1=BGR, 2=bitmask */

    /* ACPI RSDP physical address (0 if unavailable) */
    u64  rsdp;

    /* Kernel command line — embedded as bytes (pointer would dangle after ExitBootServices) */
    char cmdline[256];

    /* Optional: initrd / ramdisk */
    u64  initrd_addr;
    u64  initrd_size;
} BootInfo;

/* BIOS-specific overlay (placed at same address, cast as needed) */
typedef struct {
    BootInfo base;        /* Must be first */
    const char *cmdline;  /* shorthand kept for BIOS side */
    void  *fat_root;      /* FAT filesystem context */
} BiosBootInfo;

/*
 * OSLoader vtable.
 *
 * can_load(entry) — return true if this loader handles the given entry.
 * load(entry, info, fs_ctx) — load the kernel into memory.
 *                             fs_ctx is a platform filesystem handle
 *                             (EFI_FILE_PROTOCOL* on UEFI, NULL on BIOS).
 *                             Returns the kernel entry point address, or 0.
 * boot(entry_point, info) — transfer control to the kernel. Never returns.
 */
typedef struct {
    const char *name;
    bool        (*can_load)(const BootEntry *entry);
    u64         (*load)(const BootEntry *entry, BootInfo *info, void *fs_ctx);
    void        (*boot)(u64 entry_point, BootInfo *info);
} OSLoader;
