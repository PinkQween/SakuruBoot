/*
 * SakuruBoot — Windows Boot Manager chain-loader
 *
 * UEFI: locates bootmgfw.efi on any SFS volume, loads it with
 *       gBS->LoadImage(), and hands off via gBS->StartImage().
 *       Windows manages its own ExitBootServices — we never call it.
 *
 * BIOS: finds the first NTFS/FAT32 partition tagged as "active" (or the
 *       first NTFS partition if none is active), reads its Volume Boot
 *       Record into 0x7C00, sets DL = boot drive, and jumps to it.
 *       Windows bootmgr's VBR takes over from there.
 *
 * Integration: add to loader_registry[] in uefi_loader.c and stage2/main.c.
 */

#include "os_loader.h"

/* ------------------------------------------------------------------ */
static bool windows_can_load(const BootEntry *entry) {
    return entry->type == OS_TYPE_WINDOWS;
}

/* ------------------------------------------------------------------ */
/* UEFI path                                                           */
/* ------------------------------------------------------------------ */
#ifdef SAKURU_UEFI

#include "../uefi/efi.h"
#include "../uefi/uefi_loader.h"

/* Well-known paths for Windows Boot Manager on the EFI System Partition */
static const char * const WIN_BOOTMGR_PATHS[] = {
    "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
    "\\EFI\\Boot\\bootmgfw.efi",
    "\\bootmgfw.efi",
    NULL,
};

/* Convert ASCII path to UCS-2 in a stack buffer (path ≤ 255 chars). */
static void ascii_to_ucs2_path(CHAR16 *out, const char *src, UINTN max) {
    UINTN i = 0;
    while (*src && i + 1 < max) {
        char c = *src++;
        out[i++] = (c == '/') ? L'\\' : (CHAR16)(unsigned char)c;
    }
    out[i] = 0;
}

/* Read a file from an open SFS root into a pool buffer. */
static void *read_efi_file(EFI_FILE_PROTOCOL *root, const char *path,
                           UINTN *out_size) {
    CHAR16 wpath[256];
    ascii_to_ucs2_path(wpath, path, 256);

    EFI_FILE_PROTOCOL *fh = NULL;
    if (EFI_ERROR(root->Open(root, &fh, wpath, EFI_FILE_MODE_READ, 0)) || !fh)
        return NULL;

    /* Query file size via GetInfo */
    static const EFI_GUID fi_guid = EFI_FILE_INFO_ID;
    UINTN info_sz = 256;
    UINT8 info_buf[256];
    UINTN file_sz = 0;
    if (!EFI_ERROR(fh->GetInfo(fh, (EFI_GUID *)&fi_guid, &info_sz, info_buf)))
        file_sz = (UINTN)(*(UINT64 *)(info_buf + 8));

    void *buf = NULL;
    if (file_sz > 0 &&
        !EFI_ERROR(gBS->AllocatePool(EfiLoaderData, file_sz, &buf)) && buf) {
        UINTN rd = file_sz;
        if (EFI_ERROR(fh->Read(fh, &rd, buf)) || rd == 0) {
            gBS->FreePool(buf);
            buf = NULL;
            file_sz = 0;
        }
    }

    fh->Close(fh);
    if (out_size) *out_size = file_sz;
    return buf;
}

/* Try every WIN_BOOTMGR_PATHS on a single SFS handle.
 * On success returns an EFI_HANDLE for the loaded image. */
static EFI_STATUS try_load_bootmgr_on_handle(EFI_HANDLE sfs_handle,
                                              const char *explicit_path,
                                              EFI_HANDLE *image_out) {
    EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
    if (EFI_ERROR(gBS->HandleProtocol(sfs_handle, &sfs_guid, (void **)&sfs)) || !sfs)
        return EFI_NOT_FOUND;

    EFI_FILE_PROTOCOL *root = NULL;
    if (EFI_ERROR(sfs->OpenVolume(sfs, &root)) || !root)
        return EFI_NOT_FOUND;

    /* Paths to try: explicit entry path first, then defaults */
    const char *paths[2 + /* defaults */ 3 + 1];
    int n = 0;
    if (explicit_path && explicit_path[0])
        paths[n++] = explicit_path;
    for (int i = 0; WIN_BOOTMGR_PATHS[i]; i++)
        paths[n++] = WIN_BOOTMGR_PATHS[i];
    paths[n] = NULL;

    EFI_STATUS result = EFI_NOT_FOUND;

    for (int i = 0; paths[i] && EFI_ERROR(result); i++) {
        UINTN fsz = 0;
        void *fbuf = read_efi_file(root, paths[i], &fsz);
        if (!fbuf || fsz < 2) {
            if (fbuf) gBS->FreePool(fbuf);
            continue;
        }

        /* Sanity-check: must start with "MZ" */
        if (((UINT8 *)fbuf)[0] != 0x4D || ((UINT8 *)fbuf)[1] != 0x5A) {
            gBS->FreePool(fbuf);
            continue;
        }

        EFI_HANDLE img = NULL;

        /* Try BootPolicy=TRUE first (required on some UEFI Secure Boot impl) */
        result = gBS->LoadImage(TRUE, gImage, NULL, fbuf, fsz, &img);
        if (EFI_ERROR(result))
            result = gBS->LoadImage(FALSE, gImage, NULL, fbuf, fsz, &img);

        if (!EFI_ERROR(result) && img) {
            /* Set the DeviceHandle so Windows can find the BCD store */
            EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
            EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
            gBS->HandleProtocol(img, &li_guid, (void **)&li);
            if (li) li->DeviceHandle = sfs_handle;
            *image_out = img;
        }

        gBS->FreePool(fbuf);
    }

    root->Close(root);
    return result;
}

static u64 windows_load(const BootEntry *entry, BootInfo *info, void *fs_ctx) {
    (void)info;

    EFI_HANDLE bootmgr_handle = NULL;
    EFI_STATUS s              = EFI_NOT_FOUND;

    /* 1. Try the current boot volume first (fs_ctx is the FAT root) */
    EFI_HANDLE boot_dev = get_device_handle(fs_ctx);
    if (boot_dev) {
        s = try_load_bootmgr_on_handle(boot_dev,
                                        entry->kernel[0] ? entry->kernel : NULL,
                                        &bootmgr_handle);
    }

    /* 2. If not found, scan all SFS volumes */
    if (EFI_ERROR(s) || !bootmgr_handle) {
        EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
        EFI_HANDLE *handles = NULL;
        UINTN       count   = 0;

        if (!EFI_ERROR(gBS->LocateHandleBuffer(2, &sfs_guid, NULL,
                                                &count, &handles))) {
            for (UINTN i = 0; i < count && (!bootmgr_handle || EFI_ERROR(s)); i++) {
                s = try_load_bootmgr_on_handle(handles[i],
                                                entry->kernel[0] ? entry->kernel : NULL,
                                                &bootmgr_handle);
            }
            gBS->FreePool(handles);
        }
    }

    if (EFI_ERROR(s) || !bootmgr_handle) return 0;

    /*
     * Hand off to Windows Boot Manager.
     * Windows calls ExitBootServices itself — we must NOT call it first.
     * StartImage() does not return on success.
     */
    UINTN  exit_data_size = 0;
    CHAR16 *exit_data     = NULL;
    gBS->StartImage(bootmgr_handle, &exit_data_size, &exit_data);

    /* StartImage returned — Windows launch failed */
    gBS->UnloadImage(bootmgr_handle);
    return 0;
}

static void windows_boot(u64 ep, BootInfo *info) {
    /* Never called — windows_load() hands off via StartImage() */
    (void)ep; (void)info;
}

/* ------------------------------------------------------------------ */
#else /* BIOS path                                                     */
/* ------------------------------------------------------------------ */

#include "../bios/stage2/disk.h"
#include "../bios/stage2/fat.h"

/* MBR partition entry layout */
typedef struct __attribute__((packed)) {
    u8  status;       /* 0x80 = active/bootable */
    u8  first_chs[3];
    u8  type;
    u8  last_chs[3];
    u32 first_lba;
    u32 num_sectors;
} WinMBRPart;

/* Partition type bytes that may carry Windows */
#define PART_TYPE_NTFS      0x07 /* NTFS / exFAT                          */
#define PART_TYPE_FAT32_LBA 0x0C /* FAT32 LBA (Windows system / BCD)      */
#define PART_TYPE_FAT32     0x0B

static bool is_win_part_type(u8 t) {
    return t == PART_TYPE_NTFS || t == PART_TYPE_FAT32_LBA || t == PART_TYPE_FAT32;
}

/* VBR destination and chain-load magic */
#define VBR_LOAD_ADDR   0x7C00UL
#define VBR_BOOT_SIG    0xAA55

static u64 windows_load(const BootEntry *entry, BootInfo *info, void *fs_ctx) {
    (void)info; (void)fs_ctx; (void)entry;

    /* Read the MBR to locate Windows partition */
    static u8 mbr[512];
    disk_read(0, 1, mbr);

    WinMBRPart *parts = (WinMBRPart *)(mbr + 446);

    /* Prefer active Windows partition; fall back to first Windows partition */
    u32 win_lba     = 0;
    u32 active_lba  = 0;

    for (int i = 0; i < 4; i++) {
        if (!is_win_part_type(parts[i].type)) continue;
        if (!win_lba) win_lba = parts[i].first_lba;
        if (parts[i].status == 0x80 && !active_lba)
            active_lba = parts[i].first_lba;
    }

    u32 target_lba = active_lba ? active_lba : win_lba;
    if (!target_lba) return 0;

    /* Read the VBR into 0x7C00 */
    static u8 vbr[512];
    disk_read((u64)target_lba, 1, vbr);

    /* Validate boot signature */
    if ((u16)(vbr[510] | (vbr[511] << 8)) != VBR_BOOT_SIG) return 0;

    /* Copy VBR to 0x7C00 */
    u8 *dst = (u8 *)(uintptr_t)VBR_LOAD_ADDR;
    for (int i = 0; i < 512; i++) dst[i] = vbr[i];

    return (u64)VBR_LOAD_ADDR;
}

static void windows_boot(u64 ep, BootInfo *info) {
    (void)ep; (void)info;
    /*
     * Jump to VBR at 0x7C00.
     * Per BIOS convention: DL = boot drive number (stored by stage1 in a
     * known location; we retrieve it via disk_get_drive()).
     *
     * In 32-bit mode: ljmp $0, $0x7C00 does a direct far jump.
     * In 64-bit mode: ljmp is unsupported by GAS; use the lretq trick —
     *   push CS=0 then RIP=0x7C00, then lretq pops RIP first then CS.
     */
#if defined(__x86_64__)
    u8 drive = disk_drive();
    __asm__ volatile (
        "movb %0, %%dl\n\t"
        "pushq $0x0\n\t"          /* CS  */
        "pushq %1\n\t"            /* RIP */
        "lretq\n\t"
        :: "r"(drive), "i"((long)VBR_LOAD_ADDR)
        : "dl", "memory"
    );
    while (1) __asm__ volatile ("hlt");
#elif defined(__i386__)
    u8 drive = disk_drive();
    __asm__ volatile (
        "movb %0, %%dl\n\t"
        "ljmp $0x0, %1\n\t"
        :: "r"(drive), "i"(VBR_LOAD_ADDR)
        : "dl", "memory"
    );
    while (1) __asm__ volatile ("hlt");
#endif
}

#endif /* SAKURU_UEFI */

/* ------------------------------------------------------------------ */
const OSLoader windows_loader = {
    .name     = "Windows Boot Manager",
    .can_load = windows_can_load,
    .load     = windows_load,
    .boot     = windows_boot,
};
