/*
 * SakuruBoot Stage 2 — C entry point (runs in 64-bit long mode)
 *
 * Responsibilities:
 *   1. Detect the first FAT32 partition from the MBR
 *   2. Read and parse /sakuru.cfg
 *   3. Display the boot menu
 *   4. Load the selected kernel via the OS loader registry
 *   5. Jump to the kernel
 */

#include "../../common/config.h"
#include "../../common/menu.h"
#include "../../os/os_loader.h"
#include "disk.h"
#include "fat.h"

/* ------------------------------------------------------------------ */
/* VGA text console (80×25, colour)                                    */
/* ------------------------------------------------------------------ */
#define VGA_BASE    ((u16 *)0xB8000)
#define VGA_COLS    80
#define VGA_ROWS    25
#define VGA_ATTR(fg,bg)  ((u8)(((bg) << 4) | (fg)))

static int  g_col = 0, g_row = 0;
static u8   g_attr = VGA_ATTR(7, 0); /* Light-grey on black */

static void vga_putc(char c) {
    if (c == '\r') { g_col = 0; return; }
    if (c == '\n') { g_col = 0; g_row++; goto scroll_check; }
    VGA_BASE[g_row * VGA_COLS + g_col] = (u16)((g_attr << 8) | (u8)c);
    g_col++;
    if (g_col >= VGA_COLS) { g_col = 0; g_row++; }
scroll_check:
    if (g_row >= VGA_ROWS) {
        /* Scroll up by one row */
        for (int r = 0; r < VGA_ROWS - 1; r++)
            for (int c = 0; c < VGA_COLS; c++)
                VGA_BASE[r * VGA_COLS + c] = VGA_BASE[(r+1) * VGA_COLS + c];
        for (int c = 0; c < VGA_COLS; c++)
            VGA_BASE[(VGA_ROWS-1) * VGA_COLS + c] = (u16)((g_attr << 8) | ' ');
        g_row = VGA_ROWS - 1;
    }
}

static void vga_puts(const char *s) { while (*s) vga_putc(*s++); }

static void vga_puts_int(int n) {
    char buf[16]; int i = 15; buf[i--] = 0;
    if (n == 0) { buf[i--] = '0'; }
    while (n > 0) { buf[i--] = '0' + (n % 10); n /= 10; }
    vga_puts(&buf[i + 1]);
}

static void vga_clear(void) {
    g_attr = VGA_ATTR(13, 0); /* bright pink on black */
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        VGA_BASE[i] = (u16)((g_attr << 8) | ' ');
    g_col = 0; g_row = 0;
}

static void vga_set_color(int fg, int bg) {
    (void)bg;
    switch (fg) {
        case MENU_COLOR_HIGHLIGHT: g_attr = VGA_ATTR(0,  5); break; /* Black on Magenta  */
        case MENU_COLOR_ACCENT:    g_attr = VGA_ATTR(14, 0); break; /* Bright yellow on black */
        default:                   g_attr = VGA_ATTR(13, 0); break; /* Bright pink on black */
    }
}

/* ------------------------------------------------------------------ */
/* PS/2 keyboard (port 0x60) — simple polling                          */
/* ------------------------------------------------------------------ */

/* Scancode set 1 → ASCII (partial) */
static const char sc_to_ascii[128] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\r',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',
    0,' '
};

#define KEY_UP_SCAN   0x48
#define KEY_DOWN_SCAN 0x50

static int kbd_read_key(void) {
    /* Wait up to ~1 second (rough busy-wait) */
    for (u32 timeout = 0; timeout < 100000000U; timeout++) {
        u8 status;
        __asm__ volatile ("inb $0x64, %0" : "=a"(status));
        if (!(status & 1)) continue;

        u8 scan;
        __asm__ volatile ("inb $0x60, %0" : "=a"(scan));
        if (scan & 0x80) continue; /* Key release */

        if (scan == KEY_UP_SCAN)   return 0x100;
        if (scan == KEY_DOWN_SCAN) return 0x101;
        if (scan < 128 && sc_to_ascii[scan])
            return sc_to_ascii[scan];
    }
    return -1; /* Timeout */
}

/* ------------------------------------------------------------------ */
/* MBR partition table helpers                                          */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    u8  status;
    u8  first_chs[3];
    u8  type;
    u8  last_chs[3];
    u32 first_lba;
    u32 num_sectors;
} MBRPartEntry;

static u64 find_fat32_partition(void) {
    static u8 mbr[512];
    disk_read(0, 1, mbr);

    MBRPartEntry *parts = (MBRPartEntry *)(mbr + 446);
    for (int i = 0; i < 4; i++) {
        /* FAT32 types: 0x0B, 0x0C */
        if (parts[i].type == 0x0B || parts[i].type == 0x0C)
            return (u64)parts[i].first_lba;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Kernel auto-discovery                                               */
/* ------------------------------------------------------------------ */

struct ScanCtx {
    BootConfig *cfg;
    const char  dir[FAT_MAX_PATH];  /* directory being scanned */
    int         depth;              /* 0 = /boot, 1 = /boot/subdir */
};

/* Build "/parent/child" into out[max] */
static void path_join(const char *parent, const char *child, char *out, u32 max) {
    u32 i = 0;
    while (parent[i] && i + 1 < max) { out[i] = parent[i]; i++; }
    if (i + 1 < max) out[i++] = '/';
    u32 j = 0;
    while (child[j] && i + 1 < max) out[i++] = child[j++];
    out[i] = 0;
}

static void scan_cb(const char *name, bool is_dir, void *ctx);

static void scan_dir(BootConfig *cfg, const char *dir_path, int depth) {
    struct ScanCtx sc;
    sc.cfg   = cfg;
    sc.depth = depth;
    /* copy dir_path into sc.dir (which is const but we cast for init) */
    char *d = (char *)sc.dir;
    u32 i = 0;
    while (dir_path[i] && i + 1 < FAT_MAX_PATH) { d[i] = dir_path[i]; i++; }
    d[i] = 0;
    fat_readdir(dir_path, scan_cb, &sc);
}

static void scan_cb(const char *name, bool is_dir, void *ctx) {
    struct ScanCtx *sc = (struct ScanCtx *)ctx;

    if (is_dir) {
        if (sc->depth >= 1) return; /* only recurse one level */
        char sub[FAT_MAX_PATH];
        path_join(sc->dir, name, sub, FAT_MAX_PATH);
        scan_dir(sc->cfg, sub, sc->depth + 1);
        return;
    }

    OSType type = config_guess_type(name);
    if (type == OS_TYPE_UNKNOWN) return;

    char kernel_path[MAX_STR_LEN];
    path_join(sc->dir, name, kernel_path, MAX_STR_LEN);

    /* Look for matching initrd (Linux only) */
    char initrd_path[MAX_STR_LEN] = "";
    if (type == OS_TYPE_LINUX) {
        const char *candidates[] = { "initrd.img", "initramfs.img",
                                     "initrd", "initramfs", NULL };
        for (int i = 0; candidates[i]; i++) {
            char try_path[MAX_STR_LEN];
            path_join(sc->dir, candidates[i], try_path, MAX_STR_LEN);
            FatFile *f = fat_open(try_path);
            if (f) { fat_close(f); path_join(sc->dir, candidates[i], initrd_path, MAX_STR_LEN); break; }
        }
    }

    char display_name[MAX_STR_LEN];
    config_make_name(sc->dir, name, type, display_name, MAX_STR_LEN);
    config_add_kernel(sc->cfg, display_name, type, kernel_path,
                      initrd_path[0] ? initrd_path : (const char *)0);
}


extern const OSLoader vios_loader;
extern const OSLoader linux_loader;
extern const OSLoader windows_loader;

static const OSLoader *loader_registry[] = {
    &vios_loader,
    &linux_loader,
    &windows_loader,
    NULL,
};

static const OSLoader *find_loader(const BootEntry *entry) {
    for (int i = 0; loader_registry[i]; i++)
        if (loader_registry[i]->can_load(entry)) return loader_registry[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* stage2_main — called from entry.asm after entering long mode        */
/* ------------------------------------------------------------------ */
void stage2_main(u8 boot_drive) {
    disk_init(boot_drive);
    vga_clear();
    vga_puts("SakuruBoot Stage 2 — BIOS Edition\r\n");

    /* Locate FAT32 partition */
    u64 part_lba = find_fat32_partition();
    if (!part_lba) {
        vga_puts("ERROR: No FAT32 partition found\r\n");
        goto halt;
    }

    if (fat_init(part_lba) < 0) {
        vga_puts("ERROR: FAT32 init failed\r\n");
        goto halt;
    }

    BootConfig config;
    config.timeout       = DEFAULT_TIMEOUT;
    config.default_entry = 0;
    config.num_entries   = 0;

    /* Read sakuru.cfg if present (optional) */
    FatFile *cf = fat_open("/" CONFIG_FILE);
    if (cf) {
        static char cfg_buf[4096];
        u32 cfg_len = fat_read(cf, cfg_buf, sizeof(cfg_buf) - 1);
        fat_close(cf);
        cfg_buf[cfg_len] = 0;
        config_parse(cfg_buf, cfg_len, &config);
    }

    /* Auto-discover kernels under /boot/ */
    scan_dir(&config, "/boot", 0);

    if (config.num_entries == 0) {
        vga_puts("ERROR: No boot entries found\r\n");
        goto halt;
    }

    /* Boot menu */
    static const MenuOps menu_ops = {
        .print     = vga_puts,
        .print_int = vga_puts_int,
        .read_key  = kbd_read_key,
        .clear     = vga_clear,
        .set_color = vga_set_color,
        .cols      = VGA_COLS,
    };

    int sel = menu_run(&config, &menu_ops);
    BootEntry *entry = &config.entries[sel];

    vga_puts("\r\nLoading: ");
    vga_puts(entry->name);
    vga_puts("\r\n");

    /* Find OS loader */
    const OSLoader *loader = find_loader(entry);
    if (!loader) {
        vga_puts("ERROR: No loader for this kernel type\r\n");
        goto halt;
    }

    /* Minimal boot info for BIOS (no memory map / GOP here) */
    static BiosBootInfo bios_info;
    for (u32 i = 0; i < sizeof(bios_info); i++) ((u8*)&bios_info)[i] = 0;
    bios_info.cmdline  = entry->cmdline;
    bios_info.fat_root = (void *)0; /* FAT context — loader uses fat_open directly */

    u64 ep = loader->load(entry, &bios_info.base, NULL);
    if (!ep) {
        vga_puts("ERROR: Kernel load failed\r\n");
        goto halt;
    }

    loader->boot(ep, &bios_info.base);

halt:
    __asm__ volatile ("cli; hlt");
    while (1) {}
}
