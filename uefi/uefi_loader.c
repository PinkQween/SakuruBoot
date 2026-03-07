/*
 * SakuruBoot UEFI main loader — architecture-independent logic.
 * Works on both x86_64 and AArch64 UEFI firmware.
 */

#include "uefi_loader.h"
#include "ext4.h"
#include "../common/config.h"
#include "../common/menu.h"
#include "../common/passphrase.h"
#include "../os/os_loader.h"
#include "../luks/luks_vol.h"

/* ------------------------------------------------------------------ */
/* Globals set by uefi_main                                            */
/* ------------------------------------------------------------------ */
EFI_SYSTEM_TABLE  *gST;
EFI_BOOT_SERVICES *gBS;
EFI_HANDLE         gImage;

/* The SFS handle of the volume SakuruBoot was loaded from.
 * Set by find_boot_volume(); used to prioritise that volume in scans. */
static EFI_HANDLE g_boot_sfs_handle = NULL;

/* ------------------------------------------------------------------ */
/* Memory helpers used by crypto/argon2 and luks/luks_vol             */
/* ------------------------------------------------------------------ */
typedef EFI_STATUS (EFIAPI *AllocPoolFn)(UINTN, UINTN, void **);
typedef EFI_STATUS (EFIAPI *FreePoolFn)(void *);
#define bs_alloc_pool ((AllocPoolFn)(gBS->AllocatePool))
#define bs_free_pool  ((FreePoolFn) (gBS->FreePool))

void *gBS_alloc_pool(u32 size) {
    void *p = NULL;
    bs_alloc_pool(EfiLoaderData, (UINTN)size, &p);
    return p;
}
void gBS_free_pool(void *p) {
    if (p) bs_free_pool(p);
}

/* Active theme/accent colors — set from config before menu is shown.
 * theme_color  : 0-7  (EFI bg index: 5 = magenta by default)
 * accent_color : 0-15 (EFI fg index: 14 = yellow by default)  */
static UINTN g_theme_bg  = EFI_BACKGROUND_MAGENTA; /* theme_color << 4 */
static UINTN g_accent_fg = EFI_YELLOW;

/* Runtime services helpers (void* in header, cast here) */
typedef EFI_STATUS (EFIAPI *GetVariableFn)(CHAR16*, EFI_GUID*, UINT32*, UINTN*, void*);
typedef EFI_STATUS (EFIAPI *SetVariableFn)(CHAR16*, EFI_GUID*, UINT32, UINTN, void*);
typedef EFI_STATUS (EFIAPI *ExitFn)(EFI_HANDLE, EFI_STATUS, UINTN, CHAR16*);
#define rt_get_var ((GetVariableFn)(gST->RuntimeServices->GetVariable))
#define rt_set_var ((SetVariableFn)(gST->RuntimeServices->SetVariable))
#define bs_exit    ((ExitFn)(gBS->Exit))

typedef EFI_STATUS (EFIAPI *DisconnectControllerFn)(EFI_HANDLE, EFI_HANDLE, EFI_HANDLE);
#define bs_disconnect ((DisconnectControllerFn)(gBS->DisconnectController))

/* EFI Global Variable namespace */
#define EFI_GLOBAL_VARIABLE_GUID \
    EFI_GUID_INIT(0x8be4df61,0x93ca,0x11d2,0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c)
#define EFI_VAR_NV_BS_RT \
    (0x00000001U /* NON_VOLATILE */ | 0x00000002U /* BOOTSERVICE */ | 0x00000004U /* RUNTIME */)

/* ------------------------------------------------------------------ */
/* UTF-8 → UCS-2 helper (ASCII subset only)                            */
/* ------------------------------------------------------------------ */
static void utf8_to_ucs2(CHAR16 *dst, const char *src, UINTN max) {
    UINTN i = 0;
    while (*src && i + 1 < max) {
        dst[i++] = (CHAR16)(unsigned char)*src++;
    }
    dst[i] = 0;
}

/* UCS-2 → ASCII (ASCII range only) */
static void ucs2_to_ascii(char *dst, const CHAR16 *src, UINTN max) {
    UINTN i = 0;
    while (src[i] && i + 1 < max) { dst[i] = (char)(src[i] & 0x7F); i++; }
    dst[i] = 0;
}

/* Build "parent/child" path into out[max] */
static void efi_path_join(const char *parent, const char *child,
                          char *out, UINTN max) {
    UINTN i = 0;
    while (parent[i] && i + 1 < max) { out[i] = parent[i]; i++; }
    /* Don't add an extra '/' if parent already ends with one */
    if (i > 0 && out[i - 1] != '/' && i + 1 < max) out[i++] = '/';
    UINTN j = 0;
    /* Skip leading '/' on child to avoid double slashes */
    while (child[j] == '/') j++;
    while (child[j] && i + 1 < max) out[i++] = child[j++];
    out[i] = 0;
}

/* ------------------------------------------------------------------ */
/* Kernel auto-discovery (UEFI)                                        */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Filesystem context: wraps either a FAT volume or an ext4 volume.    */
/* OS loaders treat fs_ctx as an opaque void* passed to read_file().   */
/* ------------------------------------------------------------------ */
typedef enum { FS_FAT = 0, FS_EXT4 = 1 } FsKind;
typedef struct {
    FsKind kind;
    union {
        struct {
            EFI_FILE_PROTOCOL *fat;
            EFI_HANDLE         sfs_handle; /* SFS handle — lets kernel EFI stub
                                              locate files (initrd) on the correct
                                              device via li->DeviceHandle */
        };
        Ext4Vol *ext4;
    };
} FsCtx;

/* Per-entry filesystem contexts (set during scanning) */
static FsCtx  g_entry_ctx_store[MAX_ENTRIES];
static FsCtx *g_entry_ctx[MAX_ENTRIES]; /* NULL = not set */

/* Return the device handle for a given fs_ctx, or NULL for ext4/unknown. */
EFI_HANDLE get_device_handle(void *raw_ctx) {
    if (!raw_ctx) return NULL;
    FsCtx *ctx = (FsCtx *)raw_ctx;
    if (ctx->kind == FS_FAT) return ctx->sfs_handle;
    return NULL;
}

/* Forward declare con_puts / con_puts_int so scan helpers can emit output */
static void con_puts(const char *s);
static void con_puts_int(int n);

static void scan_dir_uefi(EFI_FILE_PROTOCOL *root, BootConfig *cfg,
                           const char *dir_path, int depth, bool allow_efi);

static EFI_FILE_PROTOCOL *open_path(EFI_FILE_PROTOCOL *root,
                                     const char *path) {
    CHAR16 wpath[256];
    int i = 0;
    while (path[i] && i < 255) {
        wpath[i] = (path[i] == '/') ? '\\' : (CHAR16)(unsigned char)path[i];
        i++;
    }
    wpath[i] = 0;
    EFI_FILE_PROTOCOL *f = NULL;
    root->Open(root, &f, wpath, EFI_FILE_MODE_READ, 0);
    return f;
}

/* Current SFS handle — set by scan_all_filesystems for each volume so that
 * scan_dir_uefi can store it in g_entry_ctx_store without an extra parameter. */
static EFI_HANDLE g_current_sfs_handle = NULL;

static void scan_dir_uefi(EFI_FILE_PROTOCOL *root, BootConfig *cfg,
                           const char *dir_path, int depth, bool allow_efi) {
    EFI_FILE_PROTOCOL *dir = open_path(root, dir_path);
    if (!dir) return;

    UINT8    info_buf[512];
    UINTN    info_size;

    while (1) {
        info_size = sizeof(info_buf);
        EFI_STATUS s = dir->Read(dir, &info_size, info_buf);
        if (EFI_ERROR(s) || info_size == 0) break;

        EFI_FILE_INFO *fi  = (EFI_FILE_INFO *)info_buf;
        char           name[256];
        ucs2_to_ascii(name, fi->FileName, sizeof(name));

        if (name[0] == '.' && (name[1] == 0 ||
            (name[1] == '.' && name[2] == 0))) continue;

        bool is_dir = !!(fi->Attribute & EFI_FILE_DIRECTORY);

        if (is_dir) {
            if (depth >= 1) continue;
            char sub[256];
            efi_path_join(dir_path, name, sub, sizeof(sub));
            scan_dir_uefi(root, cfg, sub, depth + 1, allow_efi);
            continue;
        }

        /* Determine kernel type — allow_efi lets /EFI/Linux/ *.efi be treated
         * as unified kernel images (UKI) which are self-contained. */
        OSType type = config_guess_type(name);
        if (type == OS_TYPE_UNKNOWN && allow_efi) {
            /* Check for .efi extension (UKI) */
            int nl = 0; while (name[nl]) nl++;
            if (nl > 4 && name[nl-4] == '.' &&
                (name[nl-3] == 'e' || name[nl-3] == 'E') &&
                (name[nl-2] == 'f' || name[nl-2] == 'F') &&
                (name[nl-1] == 'i' || name[nl-1] == 'I'))
                type = OS_TYPE_LINUX;
        }
        if (type == OS_TYPE_UNKNOWN) continue;

        char kernel_path[256];
        efi_path_join(dir_path, name, kernel_path, sizeof(kernel_path));

        /* Search for initrd beside the kernel (UKIs don't need one) */
        char initrd_path[256] = "";
        if (type == OS_TYPE_LINUX && !allow_efi) {
            /* Derive suffix from kernel name: "vmlinuz-linux" → "linux" */
            char suffix[64] = "";
            const char *pfx = "vmlinuz-";
            int pi = 0; while (pfx[pi] && name[pi] == pfx[pi]) pi++;
            if (pfx[pi] == 0) { /* matched "vmlinuz-" prefix */
                for (int si = 0; name[pi + si] && si < 63; si++)
                    suffix[si] = name[pi + si], suffix[si+1] = 0;
            }
            /* Build candidate list: suffix-derived names first */
            char cand_buf0[80] = "", cand_buf1[80] = "";
            if (suffix[0]) {
                /* e.g. "initramfs-linux.img", "initramfs-linux-fallback.img" */
                int k = 0;
                const char *p1 = "initramfs-"; for (; *p1; p1++) cand_buf0[k++] = *p1;
                for (int j = 0; suffix[j] && k < 74; j++) cand_buf0[k++] = suffix[j];
                const char *p2 = ".img"; for (; *p2; p2++) cand_buf0[k++] = *p2;
                cand_buf0[k] = 0;
                k = 0;
                p1 = "initramfs-"; for (; *p1; p1++) cand_buf1[k++] = *p1;
                for (int j = 0; suffix[j] && k < 70; j++) cand_buf1[k++] = suffix[j];
                p2 = "-fallback.img"; for (; *p2; p2++) cand_buf1[k++] = *p2;
                cand_buf1[k] = 0;
            }
            const char *candidates[] = {
                cand_buf0[0] ? cand_buf0 : NULL,
                cand_buf1[0] ? cand_buf1 : NULL,
                "initramfs-linux.img", "initramfs-linux-fallback.img",
                "initrd.img", "initramfs.img", "initrd", "initramfs",
                NULL
            };
            for (int i = 0; candidates[i]; i++) {
                char try_path[256];
                efi_path_join(dir_path, candidates[i], try_path, sizeof(try_path));
                EFI_FILE_PROTOCOL *tf = open_path(root, try_path);
                if (tf) { tf->Close(tf);
                    efi_path_join(dir_path, candidates[i], initrd_path, sizeof(initrd_path));
                    break;
                }
            }
        }

        char display_name[256];
        config_make_name(dir_path, name, type, display_name, sizeof(display_name));

        u32 prev = cfg->num_entries;
        config_add_kernel(cfg, display_name, type, kernel_path,
                          initrd_path[0] ? initrd_path : (const char *)0);
        if (cfg->num_entries > prev) {
            u32 idx = cfg->num_entries - 1;
            g_entry_ctx_store[idx].kind       = FS_FAT;
            g_entry_ctx_store[idx].fat        = root;
            g_entry_ctx_store[idx].sfs_handle = g_current_sfs_handle;
            g_entry_ctx[idx] = &g_entry_ctx_store[idx];
        }
    }

    dir->Close(dir);
}

/* ------------------------------------------------------------------ */
/* systemd-boot loader-entry parser                                    */
/* Reads /loader/entries/*.conf and adds entries to BootConfig.        */
/* Each .conf has: title, linux /path, initrd /path (×N), options ...  */
/* ------------------------------------------------------------------ */
static int sl_eq(const char *a, const char *b) { /* simple strcmp */
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == 0 && *b == 0);
}
static int sl_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void sl_copy(char *d, const char *s, int max) {
    int i = 0; while (i + 1 < max && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}
static void sl_cat(char *d, int max, const char *s) {
    int n = sl_len(d), i = 0;
    while (n + i + 1 < max && s[i]) { d[n + i] = s[i]; i++; } d[n + i] = 0;
}

static void scan_loader_entries(EFI_FILE_PROTOCOL *root, BootConfig *cfg,
                                 EFI_HANDLE sfs_handle) {
    EFI_FILE_PROTOCOL *dir = open_path(root, "/loader/entries");
    if (!dir) return;

    UINT8 info_buf[sizeof(EFI_FILE_INFO) + 256 * 2];
    for (;;) {
        UINTN info_sz = sizeof(info_buf);
        EFI_FILE_INFO *info = (EFI_FILE_INFO *)info_buf;
        EFI_STATUS es = dir->Read(dir, &info_sz, info);
        if (EFI_ERROR(es) || info_sz == 0) break;
        if (info->Attribute & EFI_FILE_DIRECTORY) continue;

        /* Only process .conf files */
        char fname[256];
        ucs2_to_ascii(fname, info->FileName, sizeof(fname));
        int flen = sl_len(fname);
        if (flen < 6 || fname[flen-5] != '.' || fname[flen-4] != 'c' ||
            fname[flen-3] != 'o' || fname[flen-2] != 'n' || fname[flen-1] != 'f')
            continue;

        char conf_path[256];
        efi_path_join("/loader/entries", fname, conf_path, sizeof(conf_path));
        UINTN csz = 0;
        FsCtx tmp = { .kind = FS_FAT, .fat = root };
        char *cbuf = read_file(&tmp, conf_path, &csz);
        if (!cbuf) continue;

        /* Parse the .conf file */
        char title[256] = "", linux_path[256] = "", options[256] = "";
        char initrds[4][256]; int num_initrds = 0;
        for (int k = 0; k < 4; k++) initrds[k][0] = 0;

        UINTN pos = 0;
        while (pos < csz) {
            /* Extract line */
            UINTN ls = pos;
            while (pos < csz && cbuf[pos] != '\n' && cbuf[pos] != '\r') pos++;
            UINTN le = pos;
            while (pos < csz && (cbuf[pos]=='\n'||cbuf[pos]=='\r')) pos++;
            if (le == ls || cbuf[ls] == '#') continue;

            /* Split key / value */
            UINTN ki = ls;
            while (ki < le && cbuf[ki] != ' ' && cbuf[ki] != '\t') ki++;
            UINTN vi = ki;
            while (vi < le && (cbuf[vi]==' '||cbuf[vi]=='\t')) vi++;
            if (ki == ls || vi >= le) continue;

            char key[32] = ""; char val[256] = "";
            for (UINTN x = ls; x < ki && x - ls < 31; x++) key[x-ls] = cbuf[x];
            for (UINTN x = vi; x < le && x - vi < 255; x++) val[x-vi] = cbuf[x];

            if      (sl_eq(key, "title"))   sl_copy(title,      val, 256);
            else if (sl_eq(key, "linux"))   sl_copy(linux_path, val, 256);
            else if (sl_eq(key, "initrd") && num_initrds < 4)
                sl_copy(initrds[num_initrds++], val, 256);
            else if (sl_eq(key, "options")) sl_copy(options,    val, 256);
        }
        gBS->FreePool(cbuf);

        if (!linux_path[0]) continue; /* invalid entry */

        /* Build combined cmdline: options + initrd=\path for each initrd */
        char cmdline[256] = "";
        sl_copy(cmdline, options, sizeof(cmdline));
        for (int k = 0; k < num_initrds; k++) {
            if (!initrds[k][0]) continue;
            /* Convert /path to \path for UEFI EFI stub */
            char ipath[256];
            sl_copy(ipath, initrds[k], sizeof(ipath));
            for (int j = 0; ipath[j]; j++) if (ipath[j] == '/') ipath[j] = '\\';
            if (cmdline[0]) sl_cat(cmdline, sizeof(cmdline), " ");
            sl_cat(cmdline, sizeof(cmdline), "initrd=");
            sl_cat(cmdline, sizeof(cmdline), ipath);
        }

        /* Use filename as fallback title */
        if (!title[0]) sl_copy(title, fname, sizeof(title));

        u32 prev = cfg->num_entries;
        config_add_kernel(cfg, title, OS_TYPE_LINUX, linux_path,
                          num_initrds > 0 ? initrds[0] : NULL);
        if (cfg->num_entries > prev) {
            u32 idx = cfg->num_entries - 1;
            /* Store the full cmdline on the entry */
            sl_copy(cfg->entries[idx].cmdline, cmdline, 256);
            g_entry_ctx_store[idx].kind       = FS_FAT;
            g_entry_ctx_store[idx].fat        = root;
            g_entry_ctx_store[idx].sfs_handle = sfs_handle;
            g_entry_ctx[idx] = &g_entry_ctx_store[idx];
        }
    }
    dir->Close(dir);
}

/*
 * Scan every FAT volume for kernels.  Tries:
 *   /loader/entries/  — systemd-boot entries (most complete: has root= etc.)
 *   /boot/            — most distros fallback
 *   /EFI/Linux/       — Arch UKIs (.efi)
 *   /                 — Arch ESP root (vmlinuz-linux at ESP root)
 */
static void scan_all_filesystems(BootConfig *cfg) {
    EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;

    EFI_STATUS s = gBS->LocateHandleBuffer(2 /* ByProtocol */,
                                            &sfs_guid, NULL,
                                            &count, &handles);
    if (EFI_ERROR(s) || count == 0) {
        con_puts("WARNING: No filesystem handles found\n");
        return;
    }

    /* If we know which volume we booted from, process it first so that its
     * sakuru.cfg is the "primary" config and sets theme/timeout/defaults.
     * On HP, the internal EFI partition typically appears first in
     * LocateHandleBuffer but is NOT our boot USB — processing it first
     * would make scan_loader_entries() add Linux entries (num_entries>0),
     * then the USB sakuru.cfg gets demoted to config_parse_merge and loses
     * its globals (theme, timeout, default_entry). */
    if (g_boot_sfs_handle) {
        for (UINTN i = 0; i < count; i++) {
            if (handles[i] == g_boot_sfs_handle && i != 0) {
                /* Swap boot handle to front */
                EFI_HANDLE tmp = handles[0];
                handles[0] = handles[i];
                handles[i] = tmp;
                break;
            }
        }
    }

    for (UINTN i = 0; i < count; i++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
        s = gBS->HandleProtocol(handles[i], &sfs_guid, (void **)&sfs);
        if (EFI_ERROR(s) || !sfs) continue;

        EFI_FILE_PROTOCOL *fs_root = NULL;
        s = sfs->OpenVolume(sfs, &fs_root);
        if (EFI_ERROR(s) || !fs_root) continue;

        /* Set global so scan_dir_uefi can store the SFS handle */
        g_current_sfs_handle = handles[i];

        /* sakuru.cfg — first volume that has one does a full parse
         * (sets timeout/theme/accent/entries).  Subsequent volumes with
         * their own sakuru.cfg are merged in so their entries are added
         * without clobbering the globals from the primary config. */
        UINTN cfg_sz = 0;
        FsCtx fat_ctx = { .kind = FS_FAT, .fat = fs_root,
                          .sfs_handle = handles[i] };
        char *cfg_buf = read_file(&fat_ctx, "/" CONFIG_FILE, &cfg_sz);
        if (cfg_buf) {
            u32 entries_before = cfg->num_entries;
            if (entries_before == 0)
                config_parse(cfg_buf, (unsigned)cfg_sz, cfg);
            else
                config_parse_merge(cfg_buf, (unsigned)cfg_sz, cfg);
            for (u32 j = entries_before; j < cfg->num_entries; j++) {
                if (!g_entry_ctx[j]) {
                    g_entry_ctx_store[j].kind       = FS_FAT;
                    g_entry_ctx_store[j].fat        = fs_root;
                    g_entry_ctx_store[j].sfs_handle = handles[i];
                    g_entry_ctx[j] = &g_entry_ctx_store[j];
                }
            }
            gBS->FreePool(cfg_buf);
        }

        /* systemd-boot loader entries (preferred — include root= and initrd) */
        scan_loader_entries(fs_root, cfg, handles[i]);

        /* Fallback: direct kernel scan */
        scan_dir_uefi(fs_root, cfg, "/boot",      0, false);
        scan_dir_uefi(fs_root, cfg, "/EFI/Linux", 0, true);
        scan_dir_uefi(fs_root, cfg, "/",          0, false);
    }

    gBS->FreePool(handles);
}

/* ------------------------------------------------------------------ */
/* ext4 kernel scanner                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    Ext4Vol    *vol;
    BootConfig *cfg;
    char        dir[256];
} Ext4ScanCtx;

static void ext4_scan_cb(void *raw, const char *name, bool is_dir) {
    if (is_dir) return;
    Ext4ScanCtx *sc = (Ext4ScanCtx *)raw;

    OSType type = config_guess_type(name);
    if (type == OS_TYPE_UNKNOWN) return;

    char kernel_path[256];
    efi_path_join(sc->dir, name, kernel_path, sizeof(kernel_path));

    /* For Linux kernels, search for a matching initrd */
    char initrd_path[256] = "";
    if (type == OS_TYPE_LINUX) {
        /* Standard initrd names — checked in order */
        const char *candidates[] = {
            "initramfs.img", "initrd.img",
            "initramfs", "initrd",
            "initramfs-linux.img",          /* Arch default */
            "initramfs-linux-fallback.img", /* Arch fallback — try last */
            (const char *)0
        };
        for (int i = 0; candidates[i]; i++) {
            char try_path[256];
            efi_path_join(sc->dir, candidates[i], try_path, sizeof(try_path));
            if (ext4_stat(sc->vol, try_path) > 0) {
                for (int j = 0; j < 255 && try_path[j]; j++) {
                    initrd_path[j]   = try_path[j];
                    initrd_path[j+1] = 0;
                }
                break;
            }
        }
    }

    char display_name[256];
    config_make_name(sc->dir, name, type, display_name, sizeof(display_name));

    u32 prev = sc->cfg->num_entries;
    config_add_kernel(sc->cfg, display_name, type, kernel_path,
                      initrd_path[0] ? initrd_path : (const char *)0);
    if (sc->cfg->num_entries > prev) {
        u32 idx = sc->cfg->num_entries - 1;
        g_entry_ctx_store[idx].kind = FS_EXT4;
        g_entry_ctx_store[idx].ext4 = sc->vol;
        g_entry_ctx[idx] = &g_entry_ctx_store[idx];
    }
}

/*
 * Enumerate all Block IO handles, mount any ext4 partitions found,
 * and scan /boot for kernels on each.
 */
static void scan_all_ext4(BootConfig *cfg) {
    EFI_GUID bio_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;

    EFI_STATUS s = gBS->LocateHandleBuffer(2 /* ByProtocol */,
                                            &bio_guid, NULL,
                                            &count, &handles);
    if (EFI_ERROR(s) || !count) return;

    for (UINTN i = 0; i < count; i++) {
        Ext4Vol *vol = ext4_mount(handles[i]);
        if (!vol) continue;

        /* Scan "/" — covers dedicated /boot partitions where kernels sit at
         *             the partition root, and any ESP-style root-level kernels */
        Ext4ScanCtx sc_root;
        sc_root.vol = vol;
        sc_root.cfg = cfg;
        sc_root.dir[0] = '/'; sc_root.dir[1] = 0;
        ext4_readdir(vol, "/", ext4_scan_cb, &sc_root);

        /* Scan "/boot" — covers root partitions where /boot is a subdirectory */
        Ext4ScanCtx sc_boot;
        sc_boot.vol = vol;
        sc_boot.cfg = cfg;
        const char *bp = "/boot";
        for (int j = 0; j < 6; j++) sc_boot.dir[j] = bp[j];
        ext4_readdir(vol, "/boot", ext4_scan_cb, &sc_boot);

        /* Keep vol alive — stored in g_entry_ctx_store[].ext4 */
    }

    gBS->FreePool(handles);
}



/*
 * Decode UTF-8 → UCS-2 and output via UEFI ConOut->OutputString.
 * Supports 1-byte (ASCII), 2-byte, and 3-byte UTF-8 sequences.
 * Box drawing characters in menu.h are 3-byte sequences (U+2500..U+257F);
 * sending proper Unicode code points (not raw CP437 byte values) ensures
 * correct rendering on all UEFI firmware, which uses Unicode glyph tables.
 */
static void con_puts(const char *s) {
    CHAR16 buf[128];
    int n = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s;
        CHAR16 wc;

        if (c == '\n') {
            buf[n++] = '\r';
            if (n >= 126) { buf[n] = 0; gST->ConOut->OutputString(gST->ConOut, buf); n = 0; }
            wc = '\n'; s++;
        } else if (c < 0x80) {
            wc = (CHAR16)c; s++;
        } else if ((c & 0xE0) == 0xC0 && (unsigned char)s[1] >= 0x80) {
            /* 2-byte UTF-8: U+0080..U+07FF */
            wc = (CHAR16)(((c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F));
            s += 2;
        } else if ((c & 0xF0) == 0xE0 &&
                   (unsigned char)s[1] >= 0x80 && (unsigned char)s[2] >= 0x80) {
            /* 3-byte UTF-8: U+0800..U+FFFF — box drawing chars live here */
            wc = (CHAR16)(((c & 0x0F) << 12) |
                          (((unsigned char)s[1] & 0x3F) << 6) |
                           ((unsigned char)s[2] & 0x3F));
            s += 3;
        } else {
            wc = (CHAR16)c; s++;
        }

        buf[n++] = wc;
        if (n >= 126) { buf[n] = 0; gST->ConOut->OutputString(gST->ConOut, buf); n = 0; }
    }
    if (n > 0) { buf[n] = 0; gST->ConOut->OutputString(gST->ConOut, buf); }
}

static void con_puts_int(int n) {
    char buf[16];
    int i = 15;
    buf[i--] = 0;
    if (n == 0) { buf[i--] = '0'; }
    while (n > 0) { buf[i--] = '0' + (n % 10); n /= 10; }
    con_puts(&buf[i + 1]);
}

static void con_puts_hex(UINTN n) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[20]; int i = 18; buf[19] = 0;
    buf[i] = 0; i--;
    do { buf[i--] = hex[n & 0xF]; n >>= 4; } while (n);
    buf[i--] = 'x'; buf[i] = '0';
    con_puts(&buf[i]);
}

static void con_clear(void) {
    gST->ConOut->ClearScreen(gST->ConOut);
}

static void con_set_cursor(int row, int col) {
    gST->ConOut->SetCursorPosition(gST->ConOut, (UINTN)col, (UINTN)row);
}

static void con_show_cursor(int on) {
    gST->ConOut->EnableCursor(gST->ConOut, on ? TRUE : FALSE);
}

static void con_set_color(int fg, int bg) {
    (void)bg;
    UINTN attr;
    /* Bright (light) variant of the theme color — used for border strokes */
    UINTN theme_fg = (g_theme_bg >> 4) | 0x08; /* e.g. MAGENTA(5)→LIGHTMAGENTA(13) */
    switch (fg) {
        /* ║ walls and box chars: bright theme color on black */
        case MENU_COLOR_BORDER:    attr = EFI_TEXT_ATTR(theme_fg,       EFI_BACKGROUND_BLACK); break;
        /* Title text: accent color on black */
        case MENU_COLOR_TITLE:     attr = EFI_TEXT_ATTR(g_accent_fg,    EFI_BACKGROUND_BLACK); break;
        /* Selected entry row: white text on theme background */
        case MENU_COLOR_HIGHLIGHT: attr = EFI_TEXT_ATTR(EFI_WHITE,      g_theme_bg); break;
        /* Selector » on highlight row: accent on theme background */
        case MENU_COLOR_ACCENT:    attr = EFI_TEXT_ATTR(g_accent_fg,    g_theme_bg); break;
        /* Normal unselected entries: white on black */
        case MENU_COLOR_NORMAL:    attr = EFI_TEXT_ATTR(EFI_WHITE,      EFI_BACKGROUND_BLACK); break;
        /* Countdown timer: bright theme color on black */
        case MENU_COLOR_TIMER:     attr = EFI_TEXT_ATTR(theme_fg,       EFI_BACKGROUND_BLACK); break;
        /* Hint/countdown text: accent on black */
        case MENU_COLOR_HINT:      attr = EFI_TEXT_ATTR(g_accent_fg,    EFI_BACKGROUND_BLACK); break;
        default:                   attr = EFI_TEXT_ATTR(EFI_WHITE,      EFI_BACKGROUND_BLACK); break;
    }
    gST->ConOut->SetAttribute(gST->ConOut, attr);
}

static int con_read_key(void) {
    /* Poll for a key for up to ~1 second (100 × 10 ms each).
     * Returning -1 on timeout is what drives the countdown decrement. */
    for (int i = 0; i < 100; i++) {
        EFI_INPUT_KEY key;
        EFI_STATUS s = gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
        if (!EFI_ERROR(s)) {
            if (key.ScanCode == SCAN_UP)   return 0x100;
            if (key.ScanCode == SCAN_DOWN) return 0x101;
            if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') return '\r';
            return (int)key.UnicodeChar;
        }
        gBS->Stall(10000); /* 10 ms */
    }
    return -1; /* timed out after ~1 second */
}

/* ------------------------------------------------------------------ */
/* File reading helpers                                                 */
/* ------------------------------------------------------------------ */

/*
 * Quick check: does this SFS handle have a specific file on it?
 * Used to identify which volume is our boot USB.
 */
static bool sfs_has_file(EFI_HANDLE sfs_h, const char *ascii_path) {
    EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
    if (EFI_ERROR(gBS->HandleProtocol(sfs_h, &sfs_guid, (void **)&sfs)) || !sfs)
        return false;
    EFI_FILE_PROTOCOL *vol = NULL;
    if (EFI_ERROR(sfs->OpenVolume(sfs, &vol)) || !vol) return false;

    CHAR16 wpath[256]; int i = 0;
    while (ascii_path[i] && i < 255) {
        wpath[i] = (ascii_path[i] == '/') ? L'\\' : (CHAR16)(unsigned char)ascii_path[i];
        i++;
    }
    wpath[i] = 0;

    EFI_FILE_PROTOCOL *f = NULL;
    bool found = !EFI_ERROR(vol->Open(vol, &f, wpath, EFI_FILE_MODE_READ, 0));
    if (f) f->Close(f);
    vol->Close(vol);
    return found;
}

/*
 * Find the SFS volume that SakuruBoot was loaded from.
 *
 * Strategy (most-to-least specific):
 *  1. li->DeviceHandle has SFS directly bound (QEMU virtio, some firmware)
 *  2. Any SFS handle that has /EFI/BOOT/BOOTX64.EFI   ← our file, on our USB
 *  3. Any SFS handle that has /sakuru.cfg               ← our config
 *  4. First SFS handle that opens successfully (last resort)
 *
 * Sets g_boot_sfs_handle and returns the open root EFI_FILE_PROTOCOL.
 */
static EFI_FILE_PROTOCOL *find_boot_volume(void) {
    EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID li_guid  = EFI_LOADED_IMAGE_PROTOCOL_GUID;

    /* ── 1. Loaded image's device handle ──────────────────────────── */
    EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
    gBS->HandleProtocol(gImage, &li_guid, (void **)&li);
    if (li && li->DeviceHandle) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
        gBS->HandleProtocol(li->DeviceHandle, &sfs_guid, (void **)&sfs);
        if (sfs) {
            EFI_FILE_PROTOCOL *root = NULL;
            if (!EFI_ERROR(sfs->OpenVolume(sfs, &root)) && root) {
                g_boot_sfs_handle = li->DeviceHandle;
                return root;
            }
        }
    }

    /* ── 2 & 3. File-based identification across all SFS handles ──── */
    EFI_HANDLE *handles = NULL;
    UINTN       count   = 0;
    if (EFI_ERROR(gBS->LocateHandleBuffer(2, &sfs_guid, NULL,
                                           &count, &handles)) || count == 0)
        return NULL;

    EFI_HANDLE best = NULL;

    /* Pass 1: look for /EFI/BOOT/BOOTX64.EFI (our exact bootloader file) */
    for (UINTN i = 0; i < count && !best; i++)
        if (sfs_has_file(handles[i], "/EFI/BOOT/BOOTX64.EFI"))
            best = handles[i];

    /* Pass 2: look for sakuru.cfg */
    for (UINTN i = 0; i < count && !best; i++)
        if (sfs_has_file(handles[i], "/sakuru.cfg"))
            best = handles[i];

    /* Pass 3: first handle that opens at all */
    for (UINTN i = 0; i < count && !best; i++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
        if (!EFI_ERROR(gBS->HandleProtocol(handles[i], &sfs_guid,
                                            (void **)&sfs)) && sfs)
            best = handles[i];
    }

    EFI_FILE_PROTOCOL *root = NULL;
    if (best) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
        gBS->HandleProtocol(best, &sfs_guid, (void **)&sfs);
        if (sfs) sfs->OpenVolume(sfs, &root);
        if (root) g_boot_sfs_handle = best;
    }

    gBS->FreePool(handles);
    return root;
}

char *read_file(void *ctx_ptr, const char *path, UINTN *out_size) {
    FsCtx *ctx = (FsCtx *)ctx_ptr;

    /* ext4 path */
    if (ctx->kind == FS_EXT4)
        return (char *)ext4_read_file(ctx->ext4, path, out_size);

    /* FAT path (EFI Simple File System) */
    EFI_FILE_PROTOCOL *root = ctx->fat;
    CHAR16 wpath[256];
    /* Replace forward slashes with backslashes for EFI */
    int i = 0;
    while (path[i] && i < 255) {
        wpath[i] = (path[i] == '/') ? '\\' : (CHAR16)(unsigned char)path[i];
        i++;
    }
    wpath[i] = 0;

    EFI_FILE_PROTOCOL *f = NULL;
    EFI_STATUS s = root->Open(root, &f, wpath, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s)) return NULL;

    /* Get file size */
    EFI_GUID fi_guid = EFI_FILE_INFO_GUID;
    UINT8 fi_buf[512];
    UINTN fi_sz = sizeof(fi_buf);
    s = f->GetInfo(f, &fi_guid, &fi_sz, fi_buf);
    if (EFI_ERROR(s)) { f->Close(f); return NULL; }

    EFI_FILE_INFO *fi = (EFI_FILE_INFO *)fi_buf;
    UINTN file_size   = (UINTN)fi->FileSize;

    char *buf = NULL;
    s = gBS->AllocatePool(EfiLoaderData, file_size + 1, (void **)&buf);
    if (EFI_ERROR(s)) { f->Close(f); return NULL; }

    UINTN read_sz = file_size;
    s = f->Read(f, &read_sz, buf);
    f->Close(f);
    if (EFI_ERROR(s)) { gBS->FreePool(buf); return NULL; }

    buf[read_sz] = 0;
    if (out_size) *out_size = read_sz;
    return buf;
}

/* ------------------------------------------------------------------ */
/* Gather GOP framebuffer info                                          */
/* ------------------------------------------------------------------ */
/* Write a string to COM1 (0x3F8) — safe to call before ExitBootServices */
static void gop_debug(const char *s) {
    while (*s) {
        __asm__ volatile ("outb %0, %1" : : "a"((UINT8)*s), "Nd"((UINT16)0x3F8));
        s++;
    }
}

static void gather_gop(BootInfo *info) {
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

    /* Enumerate ALL GOP handles — LocateProtocol returns only the first,
       which is often the BltOnly console GOP, not the hardware framebuffer. */
    EFI_HANDLE *handles = NULL;
    UINTN       nhandles = 0;
    EFI_STATUS  st = gBS->LocateHandleBuffer(2 /*ByProtocol*/, &gop_guid,
                                             NULL, &nhandles, &handles);
    if (EFI_ERROR(st) || !nhandles) {
        gop_debug("[GOP] no handles\r\n");
        return;
    }

    gop_debug("[GOP] scanning handles\r\n");

    typedef EFI_STATUS (EFIAPI *GopQueryMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *,
        UINT32, UINTN *, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
    typedef EFI_STATUS (EFIAPI *GopSetMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32);

    EFI_GRAPHICS_OUTPUT_PROTOCOL *best_gop = NULL;

    for (UINTN h = 0; h < nhandles; h++) {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
        st = gBS->HandleProtocol(handles[h], &gop_guid, (void **)&gop);
        if (EFI_ERROR(st) || !gop || !gop->Mode) continue;

        gop_debug("[GOP] handle: checking framebuffer\r\n");

        /* Already has a valid linear framebuffer — use it directly.
           Real framebuffers are always page-aligned; ASCII garbage never is. */
        if ((gop->Mode->FrameBufferBase & 0xFFFu) == 0 &&
            gop->Mode->FrameBufferBase >= 0x100000u &&
            gop->Mode->Info->PixelFormat != PixelBltOnly) {
            best_gop = gop;
            gop_debug("[GOP] found linear FB on current mode\r\n");
            break;
        }

        /* Try to switch to a mode that has a linear framebuffer */
        GopQueryMode query_mode = (GopQueryMode)gop->QueryMode;
        GopSetMode   set_mode   = (GopSetMode)  gop->SetMode;

        UINT32 best_m = gop->Mode->MaxMode;
        UINT32 best_w = 0;

        for (UINT32 m = 0; m < gop->Mode->MaxMode; m++) {
            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
            UINTN mi_sz = 0;
            if (EFI_ERROR(query_mode(gop, m, &mi_sz, &mi)) || !mi) continue;
            if (mi->PixelFormat == PixelBltOnly) continue;
            /* Prefer RGBX/BGRX; fall back to PixelBitMask */
            if (mi->HorizontalResolution == 1920 && mi->VerticalResolution == 1080 &&
                mi->PixelFormat != PixelBitMask) {
                best_m = m; break;
            }
            if (mi->HorizontalResolution > best_w) {
                best_w = mi->HorizontalResolution;
                best_m = m;
            }
        }

        if (best_m < gop->Mode->MaxMode) {
            set_mode(gop, best_m);
            /* Require page-aligned address ≥1 MB — same bar as initial check */
            if ((gop->Mode->FrameBufferBase & 0xFFFu) == 0 &&
                gop->Mode->FrameBufferBase >= 0x100000u) {
                best_gop = gop;
                gop_debug("[GOP] switched to linear mode\r\n");
                break;
            }
        }

        /* Keep as candidate even without FB in case it's the only GOP */
        if (!best_gop) best_gop = gop;
    }

    gBS->FreePool(handles);

    if (!best_gop ||
        (best_gop->Mode->FrameBufferBase & 0xFFFu) != 0 ||
        best_gop->Mode->FrameBufferBase < 0x100000u) {
        gop_debug("[GOP] no usable linear framebuffer found\r\n");
        return;
    }

    info->fb_addr   = best_gop->Mode->FrameBufferBase;
    info->fb_width  = best_gop->Mode->Info->HorizontalResolution;
    info->fb_height = best_gop->Mode->Info->VerticalResolution;
    info->fb_pitch  = best_gop->Mode->Info->PixelsPerScanLine * 4;
    info->fb_bpp    = 32;
    info->fb_pixel_format = (u32)best_gop->Mode->Info->PixelFormat;

    gop_debug("[GOP] framebuffer ready\r\n");
}

/* ------------------------------------------------------------------ */
/* Locate ACPI RSDP                                                     */
/* ------------------------------------------------------------------ */
static void gather_rsdp(BootInfo *info) {
    EFI_GUID acpi_guid = ACPI_20_TABLE_GUID;
    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *ct = &gST->ConfigurationTable[i];
        EFI_GUID *g = &ct->VendorGuid;
        if (g->Data1 == acpi_guid.Data1 &&
            g->Data2 == acpi_guid.Data2 &&
            g->Data3 == acpi_guid.Data3) {
            info->rsdp = (EFI_PHYSICAL_ADDRESS)(uintptr_t)ct->VendorTable;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* ExitBootServices + collect memory map                                */
/* ------------------------------------------------------------------ */
static EFI_STATUS exit_boot_services(EFI_HANDLE image, BootInfo *info) {
    UINTN mmap_size = 0, map_key, desc_size;
    UINT32 desc_ver;
    EFI_MEMORY_DESCRIPTOR *mmap = NULL;

    gBS->GetMemoryMap(&mmap_size, NULL, &map_key, &desc_size, &desc_ver);
    mmap_size += 2 * desc_size;

    EFI_STATUS s = gBS->AllocatePool(EfiLoaderData, mmap_size, (void **)&mmap);
    if (EFI_ERROR(s)) return s;

    s = gBS->GetMemoryMap(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(s)) return s;

    info->mem_map_addr        = (u64)(uintptr_t)mmap;
    info->mem_map_size        = (u64)mmap_size;
    info->mem_map_entry_size  = (u64)desc_size;
    info->mem_map_version     = desc_ver;

    return gBS->ExitBootServices(image, map_key);
}

/* ------------------------------------------------------------------ */
/* Registry of OS loaders (add new loaders here)                       */
/* ------------------------------------------------------------------ */
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
    for (int i = 0; loader_registry[i]; i++) {
        if (loader_registry[i]->can_load(entry)) return loader_registry[i];
    }
    return NULL;
}

/* Print an error in red and wait for a keypress before continuing */
static void con_error(const char *msg) {
    gST->ConOut->SetAttribute(gST->ConOut,
        EFI_TEXT_ATTR(EFI_LIGHTRED, EFI_BACKGROUND_BLACK));
    con_puts("\r\nERROR: ");
    con_puts(msg);
    con_puts("\r\n");
    gST->ConOut->SetAttribute(gST->ConOut,
        EFI_TEXT_ATTR(EFI_LIGHTMAGENTA, EFI_BACKGROUND_BLACK));
    con_puts("Press any key to continue...\r\n");
    /* Wait for keypress */
    EFI_INPUT_KEY key;
    while (EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &key)))
        gBS->Stall(10000);
}

/* ------------------------------------------------------------------ */
/* UEFI Shell launcher                                                  */
/*                                                                      */
/* 1. Firmware Volume (FV2): load the shell PE32 directly from the      */
/*    firmware binary — works on OVMF/QEMU with no files required.      */
/* 2. BootNext: set BootNext to a "Shell" boot entry + Exit to firmware */
/* 3. File fallback: /EFI/Shell/Shell.efi on the ESP                    */
/* ------------------------------------------------------------------ */
static EFI_STATUS try_load_image(void *buf, UINTN size, EFI_HANDLE *out) {
    if (!buf || size < 2) return EFI_NOT_FOUND;
    if (((UINT8*)buf)[0] != 0x4D || ((UINT8*)buf)[1] != 0x5A)
        return EFI_NOT_FOUND;  /* not MZ/PE */
    /* BootPolicy=TRUE: required on HP and other strict firmware that
     * rejects non-boot-policy loads even with Secure Boot disabled. */
    EFI_STATUS s = gBS->LoadImage(TRUE, gImage, NULL, buf, size, out);
    if (EFI_ERROR(s))
        s = gBS->LoadImage(FALSE, gImage, NULL, buf, size, out);
    return s;
}

/*
 * Load an EFI application from a known SFS volume using a proper UEFI
 * file-path device path.  More reliable than the memory-buffer approach
 * on HP and other consumer firmware (avoids EFI_SECURITY_VIOLATION when
 * LoadImage is called with NULL DevicePath on Secure Boot systems).
 *
 * Constructs: <handle's device path> + <FilePath node> + <End node>
 * then calls LoadImage with that combined path and no source buffer.
 */
static EFI_STATUS load_image_from_volume(EFI_HANDLE sfs_handle,
                                          const char *ascii_path,
                                          EFI_HANDLE *out) {
    static const EFI_GUID dp_guid  = EFI_DEVICE_PATH_PROTOCOL_GUID;
    static const EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

    /* Convert ASCII path to UCS-2 (/ → \) */
    CHAR16 wpath[256];
    int wlen = 0;
    while (ascii_path[wlen] && wlen < 255) {
        wpath[wlen] = (ascii_path[wlen] == '/') ? L'\\' :
                      (CHAR16)(unsigned char)ascii_path[wlen];
        wlen++;
    }
    wpath[wlen] = 0;
    UINTN wpath_bytes = (UINTN)(wlen + 1) * 2;

    /* Read the file into a buffer so we can pass it to LoadImage.
     * Passing BOTH device path and buffer is the most compatible approach:
     * - Device path satisfies firmware security / path-origin checks (HP)
     * - Buffer means firmware doesn't need to re-open the file (avoids
     *   mapping failures when the constructed device path can't be re-resolved) */
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs2 = NULL;
    gBS->HandleProtocol(sfs_handle, (EFI_GUID*)&sfs_guid, (void**)&sfs2);
    void  *file_buf  = NULL;
    UINTN  file_size = 0;
    if (sfs2) {
        EFI_FILE_PROTOCOL *vol_root = NULL;
        if (!EFI_ERROR(sfs2->OpenVolume(sfs2, &vol_root)) && vol_root) {
            EFI_FILE_PROTOCOL *fh = NULL;
            if (!EFI_ERROR(vol_root->Open(vol_root, &fh, wpath,
                                           EFI_FILE_MODE_READ, 0)) && fh) {
                /* Get size via EFI_FILE_INFO */
                static const EFI_GUID fi_guid = EFI_FILE_INFO_ID;
                UINTN info_sz = 256;
                UINT8 info_buf[256];
                if (!EFI_ERROR(fh->GetInfo(fh, (EFI_GUID*)&fi_guid,
                                            &info_sz, info_buf))) {
                    /* EFI_FILE_INFO: Size(8), FileSize(8), ... */
                    file_size = (UINTN)(*(UINT64*)(info_buf + 8));
                    if (file_size > 0 &&
                        !EFI_ERROR(gBS->AllocatePool(EfiLoaderData,
                                                      file_size, &file_buf))) {
                        UINTN rd = file_size;
                        if (EFI_ERROR(fh->Read(fh, &rd, file_buf))) {
                            gBS->FreePool(file_buf);
                            file_buf  = NULL;
                            file_size = 0;
                        }
                    }
                }
                fh->Close(fh);
            }
            vol_root->Close(vol_root);
        }
    }

    /* If we couldn't read the file, bail now rather than calling LoadImage
     * with neither a usable buffer nor a guarantee the path works. */
    if (!file_buf) return EFI_NOT_FOUND;

    /* Verify MZ header before handing to firmware */
    if (file_size < 2 ||
        ((UINT8*)file_buf)[0] != 0x4D || ((UINT8*)file_buf)[1] != 0x5A) {
        gBS->FreePool(file_buf);
        return EFI_NOT_FOUND;
    }

    /* Build device path: <handle's dp> + <FilePath node> + <End node> */
    EFI_DEVICE_PATH_PROTOCOL *hdp = NULL;
    gBS->HandleProtocol(sfs_handle, (EFI_GUID*)&dp_guid, (void**)&hdp);

    UINTN hdp_len = 0;
    if (hdp) {
        EFI_DEVICE_PATH_PROTOCOL *n = hdp;
        while (!(n->Type == EFI_DP_END_TYPE && n->SubType == EFI_DP_END_SUBTYPE)) {
            UINT16 sz = (UINT16)(n->Length[0] | ((UINT16)n->Length[1] << 8));
            hdp_len += sz;
            n = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8*)n + sz);
        }
    }

    UINTN  fp_node_size = 4 + wpath_bytes;
    UINTN  total        = hdp_len + fp_node_size + 4;
    UINT8 *dp_buf = NULL;
    EFI_STATUS st = EFI_OUT_OF_RESOURCES;

    if (!EFI_ERROR(gBS->AllocatePool(EfiLoaderData, total, (void**)&dp_buf))) {
        UINT8 *p = dp_buf;
        for (UINTN k = 0; k < hdp_len; k++) p[k] = ((UINT8*)hdp)[k];
        p += hdp_len;
        p[0] = EFI_DP_MEDIA_TYPE; p[1] = EFI_DP_FILEPATH_SUBTYPE;
        p[2] = (UINT8)(fp_node_size & 0xFF);
        p[3] = (UINT8)((fp_node_size >> 8) & 0xFF);
        for (UINTN k = 0; k < wpath_bytes; k++) p[4 + k] = ((UINT8*)wpath)[k];
        p += fp_node_size;
        p[0] = EFI_DP_END_TYPE; p[1] = EFI_DP_END_SUBTYPE; p[2] = 4; p[3] = 0;

        /* BootPolicy=TRUE + device path: correct for HP and strict firmware
         * that rejects non-boot-policy loads even with Secure Boot off. */
        st = gBS->LoadImage(TRUE, gImage,
                             (EFI_DEVICE_PATH_PROTOCOL*)dp_buf,
                             file_buf, file_size, out);

        /* Retry: device path but BootPolicy=FALSE */
        if (EFI_ERROR(st))
            st = gBS->LoadImage(FALSE, gImage,
                                 (EFI_DEVICE_PATH_PROTOCOL*)dp_buf,
                                 file_buf, file_size, out);

        /* Last resort: buffer only (no device path) */
        if (EFI_ERROR(st))
            st = gBS->LoadImage(FALSE, gImage, NULL,
                                 file_buf, file_size, out);

        gBS->FreePool(dp_buf);
    }

    gBS->FreePool(file_buf);
    return st;
}

static void connect_all_controllers(void); /* forward declaration */

static EFI_STATUS launch_uefi_shell(EFI_FILE_PROTOCOL *root) {
    EFI_HANDLE shell_handle = NULL;

    /* ── 1. Firmware Volume approach (OVMF built-in shell) ─────────── */
    {
        static const EFI_GUID fv2_guid   = EFI_FIRMWARE_VOLUME2_PROTOCOL_GUID;
        static const EFI_GUID shell_guid = EFI_SHELL_APP_GUID;

        UINTN      n_fv    = 0;
        EFI_HANDLE *fv_handles = NULL;
        if (!EFI_ERROR(gBS->LocateHandleBuffer(2 /* ByProtocol */,
                                                (EFI_GUID*)&fv2_guid, NULL,
                                                &n_fv, &fv_handles))) {
            for (UINTN i = 0; i < n_fv && !shell_handle; i++) {
                EFI_FIRMWARE_VOLUME2_PROTOCOL *fv = NULL;
                if (EFI_ERROR(gBS->HandleProtocol(fv_handles[i],
                                                   (EFI_GUID*)&fv2_guid,
                                                   (void**)&fv)))
                    continue;

                void  *buf  = NULL;
                UINTN  size = 0;
                UINT32 auth = 0;
                if (EFI_ERROR(fv->ReadSection(fv, (EFI_GUID*)&shell_guid,
                                               EFI_SECTION_PE32, 0,
                                               &buf, &size, &auth)))
                    continue;

                try_load_image(buf, size, &shell_handle);
                gBS->FreePool(buf);
            }
            gBS->FreePool(fv_handles);
        }
    }

    /* ── 2. BootNext: enumerate existing Boot#### vars for a "Shell" entry.
     *       Use GetNextVariableName instead of brute-forcing GetVariable on
     *       Boot0000..Boot00FF — on real hardware each non-existent NVRAM read
     *       can take 10-20 ms; 256 calls causes a visible freeze. ─────────── */
    if (!shell_handle) {
        static const EFI_GUID global_guid = EFI_GLOBAL_VARIABLE_GUID;
        UINT16 found = 0xFFFF;

        typedef EFI_STATUS (EFIAPI *GetNextVarNameFn)(UINTN *, CHAR16 *, EFI_GUID *);
        GetNextVarNameFn get_next =
            (GetNextVarNameFn)gST->RuntimeServices->GetNextVariableName;

        CHAR16   vname[256];
        EFI_GUID vguid;
        /* Zero name + guid to start enumeration from the beginning */
        vname[0] = 0;
        for (int bi = 0; bi < (int)sizeof(vguid); bi++) ((UINT8*)&vguid)[bi] = 0;

        while (found == 0xFFFF) {
            UINTN vsz = sizeof(vname);
            if (EFI_ERROR(get_next(&vsz, vname, &vguid))) break;

            /* Only look at EFI Global Variable namespace */
            if (vguid.Data1 != global_guid.Data1 ||
                vguid.Data2 != global_guid.Data2 ||
                vguid.Data3 != global_guid.Data3) continue;

            /* Must be exactly "Boot####" — 4 + 4 hex digits, null-terminated */
            if (vname[0]!='B'||vname[1]!='o'||vname[2]!='o'||vname[3]!='t'||vname[8]!=0)
                continue;
            int is_boot_hex = 1;
            for (int j = 4; j < 8; j++) {
                CHAR16 c = vname[j];
                if (!((c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f')))
                    { is_boot_hex = 0; break; }
            }
            if (!is_boot_hex) continue;

            UINT8 opt[512]; UINTN sz = sizeof(opt); UINT32 attr = 0;
            if (EFI_ERROR(rt_get_var(vname, (EFI_GUID*)&global_guid,
                                      &attr, &sz, opt)) || sz < 8) continue;

            CHAR16 *desc = (CHAR16*)(opt + 6);
            for (UINTN j = 0; desc[j]; j++) {
                if (desc[j]=='S' && desc[j+1]=='h' && desc[j+2]=='e'
                                 && desc[j+3]=='l' && desc[j+4]=='l') {
                    /* Decode the boot entry number from the 4 hex digits */
                    UINT16 num = 0;
                    for (int k = 4; k < 8; k++) {
                        CHAR16 c = vname[k];
                        int d = (c>='0'&&c<='9') ? (int)(c-'0') :
                                (c>='A'&&c<='F') ? (int)(c-'A'+10) :
                                                   (int)(c-'a'+10);
                        num = (UINT16)((num << 4) | (UINT16)d);
                    }
                    found = num;
                    break;
                }
            }
        }

        if (found != 0xFFFF) {
            static const CHAR16 bn[] = {'B','o','o','t','N','e','x','t',0};
            UINT16 next = found;
            if (!EFI_ERROR(rt_set_var((CHAR16*)bn, (EFI_GUID*)&global_guid,
                                       EFI_VAR_NV_BS_RT, sizeof(next), &next))) {
                con_puts("\r\nHanding off to firmware shell...\r\n");
                bs_exit(gImage, EFI_SUCCESS, 0, NULL);
            }
        }
    }

    /* ── 3. File fallback: Shell.efi on any FAT volume ──────────────── */
    if (!shell_handle) {
        static const char *paths[] = {
            "/EFI/Shell/Shell.efi", "/EFI/BOOT/Shell.efi", "/shellx64.efi", NULL
        };

        /* First attempt: use 'root' directly — it's the already-identified
         * boot volume (our USB), open-and-verified by find_boot_volume().
         * This avoids SFS re-enumeration issues on HP firmware. */
        if (root) {
            for (int i = 0; paths[i] && !shell_handle; i++) {
                /* Convert path to UCS-2 */
                CHAR16 wpath[128]; int wl = 0;
                while (paths[i][wl] && wl < 127) {
                    wpath[wl] = (paths[i][wl] == '/') ? L'\\' :
                                (CHAR16)(unsigned char)paths[i][wl];
                    wl++;
                }
                wpath[wl] = 0;
                EFI_FILE_PROTOCOL *fh = NULL;
                if (EFI_ERROR(root->Open(root, &fh, wpath,
                                          EFI_FILE_MODE_READ, 0)) || !fh)
                    continue;
                /* Get file size */
                static const EFI_GUID fi2 = EFI_FILE_INFO_ID;
                UINTN isz = 256; UINT8 ibuf[256];
                UINTN fsz = 0;
                if (!EFI_ERROR(fh->GetInfo(fh, (EFI_GUID*)&fi2, &isz, ibuf)))
                    fsz = (UINTN)(*(UINT64*)(ibuf + 8));
                void *sbuf = NULL;
                if (fsz > 0 &&
                    !EFI_ERROR(gBS->AllocatePool(EfiLoaderData, fsz, &sbuf))) {
                    UINTN rd = fsz;
                    if (!EFI_ERROR(fh->Read(fh, &rd, sbuf)) && rd >= 2 &&
                        ((UINT8*)sbuf)[0] == 0x4D && ((UINT8*)sbuf)[1] == 0x5A) {
                        /* Load with both boot-volume device path + buffer */
                        EFI_STATUS ls = EFI_NOT_FOUND;
                        if (g_boot_sfs_handle)
                            ls = load_image_from_volume(g_boot_sfs_handle,
                                                         paths[i], &shell_handle);
                        /* Buffer-only fallback — try BootPolicy=TRUE first (HP quirk) */
                        if (EFI_ERROR(ls))
                            ls = gBS->LoadImage(TRUE, gImage, NULL,
                                                 sbuf, rd, &shell_handle);
                        if (EFI_ERROR(ls))
                            ls = gBS->LoadImage(FALSE, gImage, NULL,
                                                 sbuf, rd, &shell_handle);
                        con_puts("Shell direct-load: ");
                        con_puts(paths[i]); con_puts(" -> ");
                        con_puts_hex((UINTN)ls); con_puts("\r\n");
                    }
                    gBS->FreePool(sbuf);
                }
                fh->Close(fh);
            }
        }

        /* Second attempt: scan every SFS volume.
         * Re-connect controllers first — on HP and similar firmware the SFS
         * handles may not be enumerated yet at this point in boot. */
        if (!shell_handle) {
            connect_all_controllers();
            gBS->Stall(300000); /* 300 ms — let HP firmware bind FAT driver */
            EFI_GUID sfs_guid2 = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
            EFI_HANDLE *sfs_handles = NULL; UINTN sfs_cnt = 0;
            con_puts("Scanning SFS volumes for Shell.efi...\r\n");
            if (!EFI_ERROR(gBS->LocateHandleBuffer(2, &sfs_guid2, NULL,
                                                    &sfs_cnt, &sfs_handles))) {
                con_puts("SFS count: "); con_puts_int((int)sfs_cnt);
                con_puts("\r\n");
                for (UINTN hi = 0; hi < sfs_cnt && !shell_handle; hi++) {
                    for (int i = 0; paths[i] && !shell_handle; i++) {
                        EFI_STATUS ls = load_image_from_volume(sfs_handles[hi],
                                                                paths[i],
                                                                &shell_handle);
                        if (!EFI_ERROR(ls)) {
                            con_puts("Found Shell on vol ");
                            con_puts_int((int)hi);
                            con_puts(" at "); con_puts(paths[i]);
                            con_puts("\r\n");
                        }
                    }
                }
                gBS->FreePool(sfs_handles);
            } else {
                con_puts("LocateHandleBuffer(SFS) failed\r\n");
            }
        }
    }

    if (!shell_handle) {
        con_error("UEFI Shell not found.\n"
                  "  Tried: /EFI/Shell/Shell.efi, /EFI/BOOT/Shell.efi, /shellx64.efi\n"
                  "  on boot volume (direct) and all SFS volumes.\n"
                  "  Re-run write-usb.sh to add Shell.efi to the USB automatically.");
        return EFI_NOT_FOUND;
    }

    UINTN exit_size = 0; CHAR16 *exit_data = NULL;
    return gBS->StartImage(shell_handle, &exit_size, &exit_data);
}

/* ------------------------------------------------------------------ */
/* Connect all UEFI controllers so block devices (eMMC, NVMe, SD)     */
/* that haven't had their filesystem drivers bound yet get enumerated. */
/* Without this, LocateHandleBuffer for SFS/BlockIO misses volumes     */
/* that are only present on mmcblk or other non-boot devices.          */
/* ------------------------------------------------------------------ */
typedef EFI_STATUS (EFIAPI *ConnectControllerFn)(EFI_HANDLE, EFI_HANDLE*,
                                                   void*, BOOLEAN);

static void connect_all_controllers(void) {
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    /* SearchType 0 = AllHandles */
    if (EFI_ERROR(gBS->LocateHandleBuffer(0, NULL, NULL, &count, &handles)))
        return;
    ConnectControllerFn connect_ctrl =
        (ConnectControllerFn)gBS->ConnectController;
    for (UINTN i = 0; i < count; i++)
        connect_ctrl(handles[i], NULL, NULL, TRUE /* Recursive */);
    gBS->FreePool(handles);
}

/* ------------------------------------------------------------------ */
/* Detach OVMF's Terminal driver from all serial devices so that       */
/* UEFI ConOut (boot menu) only renders on the GOP framebuffer and     */
/* the serial port carries only explicit OS UART output.               */
/* ------------------------------------------------------------------ */
static void detach_serial_console(void) {
    static const EFI_GUID serial_guid = EFI_SERIAL_IO_PROTOCOL_GUID;
    UINTN n = 0; EFI_HANDLE *h = NULL;
    if (EFI_ERROR(gBS->LocateHandleBuffer(2 /* ByProtocol */,
                                           (EFI_GUID*)&serial_guid,
                                           NULL, &n, &h)))
        return;
    for (UINTN i = 0; i < n; i++)
        bs_disconnect(h[i], NULL, NULL);  /* disconnect Terminal driver */
    gBS->FreePool(h);
}

/* ------------------------------------------------------------------ */
/* uefi_main — called from arch entry                                   */
/* ------------------------------------------------------------------ */
EFI_STATUS uefi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    gST    = st;
    gBS    = st->BootServices;
    gImage = image;

    gST->ConOut->ClearScreen(gST->ConOut);
    con_puts("SakuruBoot  -  Scanning disks...\r\n");

    /* Connect all block device drivers so LocateHandleBuffer finds all
     * volumes (eMMC, NVMe, SD, USB disks).
     * Then poll until OUR boot volume is accessible, identified by the
     * presence of /EFI/BOOT/BOOTX64.EFI on one of the SFS handles.
     * This is far more reliable than count-based stabilisation: on HP the
     * internal EFI partition (cnt=1) appears long before the USB partition
     * (cnt=2), and the old "stable for 2 polls" would exit with cnt=1,
     * never discovering the USB.  Here we wait up to 10 s for our file. */
    connect_all_controllers();
    {
        EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
        for (int attempt = 0; attempt < 20; attempt++) { /* max 10 s */
            EFI_HANDLE *tmp = NULL; UINTN cnt = 0;
            bool found = false;
            if (!EFI_ERROR(gBS->LocateHandleBuffer(2, &sfs_guid, NULL,
                                                    &cnt, &tmp))) {
                for (UINTN j = 0; j < cnt && !found; j++)
                    if (sfs_has_file(tmp[j], "/EFI/BOOT/BOOTX64.EFI") ||
                        sfs_has_file(tmp[j], "/sakuru.cfg"))
                        found = true;
                if (tmp) gBS->FreePool(tmp);
            }
            if (found) break;
            gBS->Stall(500000); /* 500 ms per attempt */
        }
    }

    /* Identify and open the boot filesystem volume. */
    EFI_FILE_PROTOCOL *root = find_boot_volume();
    if (!root)
        con_puts("Warning: boot filesystem not directly accessible; scanning all volumes...\r\n");

    BootConfig config;
    config.timeout       = DEFAULT_TIMEOUT;
    config.default_entry = 0;
    config.num_entries   = 0;
    config.theme_color   = 5;   /* magenta */
    config.accent_color  = 14;  /* yellow  */

    scan_all_filesystems(&config);
    scan_all_ext4(&config);

    /* Safety net: force-scan the boot volume root even if scan_all_filesystems
     * missed it (HP/consumer firmware sometimes drops the USB SFS handle from
     * LocateHandleBuffer after connect_all_controllers reconnects devices).
     * scan_dir_uefi uses config_add_kernel which deduplicates, so this is safe. */
    if (root) {
        static FsCtx boot_fs_ctx;
        boot_fs_ctx.kind = FS_FAT;
        boot_fs_ctx.fat  = root;
        boot_fs_ctx.sfs_handle = g_boot_sfs_handle;
        /* Also try sakuru.cfg on the boot volume if not yet parsed */
        if (config.num_entries == 0) {
            UINTN cfg_sz = 0;
            char *cfg_buf = read_file(&boot_fs_ctx, "/sakuru.cfg", &cfg_sz);
            if (cfg_buf) {
                config_parse(cfg_buf, (unsigned)cfg_sz, &config);
                for (u32 j = 0; j < config.num_entries; j++) {
                    if (!g_entry_ctx[j]) {
                        g_entry_ctx_store[j].kind = FS_FAT;
                        g_entry_ctx_store[j].fat  = root;
                        g_entry_ctx_store[j].sfs_handle = g_boot_sfs_handle;
                        g_entry_ctx[j] = &g_entry_ctx_store[j];
                    }
                }
                gBS->FreePool(cfg_buf);
            }
        }
        g_current_sfs_handle = g_boot_sfs_handle;
        scan_dir_uefi(root, &config, "/boot",      0, false);
        scan_dir_uefi(root, &config, "/EFI/Linux", 0, true);
    }

    /* Always offer a UEFI Shell entry at the end of the menu */
    if (config.num_entries < MAX_ENTRIES) {
        BootEntry *sh = &config.entries[config.num_entries];
        static const char shell_name[] = "UEFI Shell";
        for (int i = 0; i < (int)sizeof(shell_name); i++)
            sh->name[i] = shell_name[i];
        sh->type = OS_TYPE_UEFI_SHELL;
        sh->kernel[0]  = '\0';
        sh->initrd[0]  = '\0';
        sh->cmdline[0] = '\0';
        config.num_entries++;
    }

    if (config.num_entries == 0) {
        con_error("No bootable kernels found on any disk.\r\n"
                  "  Searched: /boot/ and /EFI/Linux/ on each FAT volume\r\n"
                  "            /boot/ on each ext4 partition\r\n"
                  "  Note: kernels on btrfs/xfs are not yet supported.");
        return EFI_LOAD_ERROR;
    }

    /* Apply configured theme/accent colors — used by con_set_color */
    g_theme_bg  = (UINTN)(config.theme_color & 0x07) << 4;
    g_accent_fg = (UINTN)(config.accent_color & 0x0F);

    /* Show boot menu */
    /* Query actual console dimensions so centering is exact */
    UINTN con_cols = 80, con_rows = 25;
    if (gST->ConOut && gST->ConOut->Mode) {
        UINT32 mode_num = gST->ConOut->Mode->Mode;
        gST->ConOut->QueryMode(gST->ConOut, (UINTN)mode_num, &con_cols, &con_rows);
    }

    static MenuOps menu_ops;
    menu_ops.print       = con_puts;
    menu_ops.print_int   = con_puts_int;
    menu_ops.read_key    = con_read_key;
    menu_ops.clear       = con_clear;
    menu_ops.set_color   = con_set_color;
    menu_ops.set_cursor  = con_set_cursor;
    menu_ops.show_cursor = con_show_cursor;
    menu_ops.cols        = (int)con_cols;
    menu_ops.rows        = (int)con_rows;

    int sel = menu_run(&config, &menu_ops);
    BootEntry *entry = &config.entries[sel];

    con_puts("\nBooting: ");
    con_puts(entry->name);
    con_puts("\n");

    /* UEFI Shell — launch without ExitBootServices */
    if (entry->type == OS_TYPE_UEFI_SHELL) {
        return launch_uefi_shell(root);
    }

    /* ----------------------------------------------------------------
     * LUKS unlock (if this entry has encrypted = yes)
     * We resolve the filesystem context for this entry, then try to
     * mount it as a LUKS volume using a prompted passphrase.
     * On success, the ext4 layer is re-mounted on top of the LUKS vol.
     * ---------------------------------------------------------------- */
    static FsCtx fallback_fat_ctx;
    fallback_fat_ctx.kind = FS_FAT;
    fallback_fat_ctx.fat  = root;
    FsCtx *load_ctx = g_entry_ctx[sel] ? g_entry_ctx[sel] : &fallback_fat_ctx;

    if (entry->encrypted) {
        /* We only support LUKS on ext4 volumes (block device access required) */
        if (load_ctx->kind == FS_EXT4 && load_ctx->ext4) {
            /* Get the raw EFI block IO handle backing this ext4 volume */
            /* Note: ext4_get_part_handle() is provided by ext4.c */
            EFI_HANDLE part_handle = ext4_get_part_handle(load_ctx->ext4);
            if (part_handle) {
                /* Build a LuksReadFn that reads raw sectors via EFI BlockIO */
                typedef EFI_STATUS (EFIAPI *BlkReadFn)
                    (void*, UINT32, EFI_LBA, UINTN, void*);
                typedef struct {
                    void     *Revision;
                    void     *Media;
                    BlkReadFn ReadBlocks;
                } EFI_BIO_SIMPLE;
                typedef EFI_BIO_SIMPLE *EFI_BLOCK_IO;
                static EFI_GUID bio_guid = {0x964e5b21,0x6459,0x11d2,
                    {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
                EFI_BLOCK_IO bio = NULL;
                gBS->HandleProtocol(part_handle, &bio_guid, (void**)&bio);

                if (bio) {
                    /* Passphrase prompt loop */
                    static u8 passphrase_buf[256];
                    int luks_ok = 0;
                    int tries = (entry->luks_tries > 0) ? entry->luks_tries : 3;

                    /* If a key file is specified, read it from the boot FAT */
                    if (entry->luks_keyfile[0] && root) {
                        EFI_FILE_PROTOCOL *kf = NULL;
                        CHAR16 kfw[256]; utf8_to_ucs2(kfw, entry->luks_keyfile, 256);
                        if (!root->Open(root, &kf, kfw, 1 /*EFI_FILE_MODE_READ*/, 0)) {
                            UINTN kfsz = 255;
                            kf->Read(kf, &kfsz, passphrase_buf);
                            passphrase_buf[kfsz] = 0;
                            kf->Close(kf);
                            tries = 1; /* single attempt with key file */
                        }
                    }

                    for (int attempt = 0; attempt < tries && !luks_ok; attempt++) {
                        u32 plen = 0;
                        if (!entry->luks_keyfile[0]) {
                            char prompt[128] = "LUKS passphrase";
                            if (attempt > 0) {
                                prompt[15] = ' '; prompt[16]='(';
                                prompt[17] = (char)('1'+attempt);
                                prompt[18] = '/';
                                prompt[19] = (char)('0'+tries);
                                prompt[20] = ')'; prompt[21] = 0;
                            }
                            con_puts("  "); con_puts(prompt); con_puts(": ");
                            plen = passphrase_read(passphrase_buf, sizeof(passphrase_buf),
                                                   &menu_ops, "");
                        } else {
                            /* already filled from key file */
                            for (plen=0; passphrase_buf[plen]; plen++);
                        }

                        /* Build a LUKS read callback over EFI BlockIO */
                        typedef struct { EFI_BLOCK_IO bio; } BlkCtx;
                        /* Use a simple inline lambda via static */
                        static EFI_BLOCK_IO s_bio;
                        s_bio = bio;

                        /* Try to open the LUKS volume */
                        /* We read directly via BlockIO using luks_open */
                        /* The LuksReadFn adapter is defined inline below */
                        LuksVol *lv = luks_open_efi(bio, passphrase_buf, plen);
                        passphrase_wipe(passphrase_buf, sizeof(passphrase_buf));

                        if (lv) {
                            /* Re-mount ext4 on top of the LUKS volume */
                            ext4_unmount(load_ctx->ext4);
                            load_ctx->ext4 = ext4_mount_luks(lv);
                            if (load_ctx->ext4) {
                                luks_ok = 1;
                                con_puts("  LUKS: unlocked\n");
                            } else {
                                luks_vol_close(lv);
                                con_error("LUKS: ext4 mount on decrypted volume failed");
                            }
                        } else {
                            con_error("LUKS: wrong passphrase or unsupported format");
                        }
                    }
                    if (!luks_ok) {
                        con_error("LUKS: failed to unlock after maximum attempts");
                        return EFI_ACCESS_DENIED;
                    }
                }
            }
        } else {
            /* FAT-based entries with encrypted=yes: inject cryptdevice only via cmdline */
            con_puts("  Note: LUKS passphrase-only mode (FAT boot partition)\n");
            con_puts("  The initrd will handle LUKS unlock.\n");
        }
    }

    /* Find appropriate OS loader */
    const OSLoader *loader = find_loader(entry);
    if (!loader) {
        con_error("No loader available for this entry type");
        return EFI_LOAD_ERROR;
    }

    /* Prepare boot info */
    BootInfo *boot_info = NULL;
    EFI_STATUS s2 = gBS->AllocatePool(EfiLoaderData, sizeof(*boot_info), (void **)&boot_info);
    if (EFI_ERROR(s2) || !boot_info) {
        con_error("Out of memory allocating boot info");
        return EFI_LOAD_ERROR;
    }
    for (UINTN i = 0; i < sizeof(*boot_info); i++) ((UINT8*)boot_info)[i] = 0;
    /* Copy cmdline into BootInfo — the entry pointer dangles after ExitBootServices */
    {
        const char *src = entry->cmdline;
        char *dst = boot_info->cmdline;
        int i = 0;
        while (i < 255 && src[i]) { dst[i] = src[i]; i++; }
        dst[i] = '\0';
    }

    gather_gop(boot_info);
    gather_rsdp(boot_info);

    /* Load kernel — use the filesystem context where this entry was found */
    if (!load_ctx->fat && load_ctx->kind == FS_FAT) {
        con_error("Kernel load failed - no filesystem context for this entry");
        return EFI_LOAD_ERROR;
    }
    uint64_t entry_point = loader->load(entry, boot_info, load_ctx);
    if (!entry_point) {
        con_error("Kernel load failed - check that the file exists and is valid");
        return EFI_LOAD_ERROR;
    }

    /* Exit boot services, get final memory map */
    EFI_STATUS s = exit_boot_services(image, boot_info);
    if (EFI_ERROR(s)) {
        con_error("ExitBootServices failed");
        return s;
    }

    /* Hand off to kernel — no return */
    loader->boot(entry_point, boot_info);

    /* Should never reach here */
    while (1) {
#if defined(__x86_64__)
        __asm__ volatile ("hlt");
#else
        __asm__ volatile ("wfi");
#endif
    }
    return EFI_SUCCESS;
}
