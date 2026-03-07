#pragma once

#include "types.h"
#include "version.h"

#define CONFIG_FILE      "sakuru.cfg"
#define MAX_ENTRIES      16
#define MAX_STR_LEN      256
#define DEFAULT_TIMEOUT  5

/* Supported kernel/loader types */
typedef enum {
    OS_TYPE_ELF64,       /* Generic ELF64 kernel (ViOS default) */
    OS_TYPE_LINUX,       /* Linux bzImage + optional initrd       */
    OS_TYPE_MULTIBOOT2,  /* Multiboot2-compliant kernel            */
    OS_TYPE_UEFI_SHELL,  /* Launch UEFI interactive shell          */
    OS_TYPE_UNKNOWN,
} OSType;

typedef struct {
    char    name[MAX_STR_LEN];    /* Display name in boot menu  */
    OSType  type;                 /* Loader type                */
    char    kernel[MAX_STR_LEN];  /* Path to kernel image       */
    char    initrd[MAX_STR_LEN];  /* Optional initrd/ramdisk    */
    char    cmdline[MAX_STR_LEN]; /* Kernel command-line args   */
} BootEntry;

typedef struct {
    u32       timeout;                   /* Menu timeout (seconds)      */
    u32       default_entry;             /* Index of default selection  */
    u32       num_entries;               /* Number of boot entries      */
    u8        theme_color;  /* Border/selection bg, 0-7  (EFI bg index). Default 5 = magenta */
    u8        accent_color; /* Title/arrow/hint fg, 0-15 (EFI fg index). Default 14 = yellow */
    BootEntry entries[MAX_ENTRIES];      /* Boot entries array          */
} BootConfig;

/*
 * Parse a null-terminated config buffer into cfg.
 * Returns 0 on success, -1 on parse error.
 */
int config_parse(const char *buf, u32 len, BootConfig *cfg);

/*
 * Like config_parse but appends entries without resetting the config.
 * Use when merging a second sakuru.cfg found on another volume.
 */
int config_parse_merge(const char *buf, u32 len, BootConfig *cfg);

/* Map "type = xxx" string to OSType enum */
OSType config_parse_type(const char *str);

/*
 * Detect OS type from a kernel filename.
 * Returns OS_TYPE_UNKNOWN if the file doesn't look like a kernel.
 */
OSType config_guess_type(const char *filename);

/*
 * Build a human-readable display name from the directory path and filename.
 * e.g. dir="/boot/vios"  file="kernel.elf" → "ViOS"
 *      dir="/boot"       file="vmlinuz"    → "Linux"
 *      dir="/boot"       file="vmlinuz-6.1"→ "Linux 6.1"
 *      dir="/boot"       file="myos.elf"   → "myos"
 */
void config_make_name(const char *dir, const char *filename, OSType type,
                      char *out, u32 max);

/*
 * Parse a color name ("yellow", "magenta", "cyan", etc.) to an EFI
 * foreground color index (0-15).  Returns -1 if the name is unknown.
 * For theme_color (bg), the caller clamps the result to 0-7.
 */
int config_parse_color(const char *name);
int config_add_kernel(BootConfig *cfg, const char *name, OSType type,
                      const char *kernel_path, const char *initrd_path);
