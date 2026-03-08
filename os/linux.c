/*
 * Generic Linux bzImage loader for SakuruBoot.
 *
 * Supports loading a Linux x86_64 bzImage with an optional initrd.
 * Implements the Linux x86 Boot Protocol (version 2.07+).
 *
 * Reference: Documentation/x86/boot.rst in the Linux kernel tree.
 */

#include "os_loader.h"
#include "elf_loader.h"

/* ------------------------------------------------------------------ */
/* Linux x86 Boot Protocol structures                                  */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    u8  setup_sects;
    u16 root_flags;
    u32 syssize;
    u16 ram_size;
    u16 vid_mode;
    u16 root_dev;
    u16 boot_flag;      /* 0xAA55 */
    u16 jump;
    u32 header;         /* "HdrS" = 0x53726448 */
    u16 version;
    u32 realmode_swtch;
    u16 start_sys_seg;
    u16 kernel_version;
    u8  type_of_loader;
    u8  loadflags;
    u16 setup_move_size;
    u32 code32_start;
    u32 ramdisk_image;
    u32 ramdisk_size;
    u32 bootsect_kludge;
    u16 heap_end_ptr;
    u8  ext_loader_ver;
    u8  ext_loader_type;
    u32 cmd_line_ptr;
    u32 initrd_addr_max;
    u32 kernel_alignment;
    u8  relocatable_kernel;
    u8  min_alignment;
    u16 xloadflags;
    u32 cmdline_size;
    u32 hardware_subarch;
    u64 hardware_subarch_data;
    u32 payload_offset;
    u32 payload_length;
    u64 setup_data;
    u64 pref_address;
    u32 init_size;
    u32 handover_offset;
} LinuxSetupHeader;

#define LINUX_MAGIC    0x53726448U   /* "HdrS" */
#define LOADED_HIGH    (1 << 0)
#define CAN_USE_HEAP   (1 << 7)
#define LOADER_TYPE    0xFF          /* undefined loader */

/* Boot params page (zero page) at 0x90000 */
#define BOOT_PARAMS_ADDR  0x90000UL
#define CMDLINE_ADDR      0x91000UL  /* Command line just after boot params */
#define SETUP_ADDR        0x90000UL  /* Setup code loaded here              */
#define KERNEL_ADDR       0x100000UL /* High kernel loaded at 1 MB          */

/* ------------------------------------------------------------------ */
#ifdef SAKURU_UEFI
#  include "../uefi/efi.h"
#  include "../uefi/uefi_loader.h"
extern char *read_file(void *root, const char *path, UINTN *out);
#else
#  include "../bios/stage2/fat.h"
static u8 linux_kernel_buf[16 * 1024 * 1024]; /* 16 MB for kernel  */
static u8 linux_initrd_buf[32 * 1024 * 1024]; /* 32 MB for initrd  */
#endif

/* ------------------------------------------------------------------ */
static bool linux_can_load(const BootEntry *entry) {
    return entry->type == OS_TYPE_LINUX;
}

static void str_copy_n(char *dst, const char *src, u32 max) {
    u32 i = 0;
    while (i + 1 < max && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static __attribute__((unused)) u32 str_len(const char *s) {
    u32 n = 0; while (s[n]) n++; return n;
}

static u64 linux_load(const BootEntry *entry, BootInfo *info, void *fs_ctx) {
#ifdef SAKURU_UEFI
    /*
     * UEFI path — use the kernel's built-in EFI stub.
     *
     * LoadImage() loads the bzImage as a UEFI application (the EFI stub
     * embedded in every modern kernel handles its own boot protocol setup,
     * ExitBootServices, memory mapping, etc.).
     * StartImage() hands control to it; on success it never returns.
     */
    UINTN ksz = 0;
    void *kbuf = read_file(fs_ctx, entry->kernel, &ksz);
    if (!kbuf || ksz < 512) return 0;

    EFI_HANDLE kernel_handle = NULL;
    EFI_STATUS s = gBS->LoadImage(FALSE, gImage, NULL, kbuf, ksz, &kernel_handle);
    if (EFI_ERROR(s)) return 0;

    /* Pass command line + initrd paths to the kernel via EFI_LOADED_IMAGE_PROTOCOL */
    EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
    gBS->HandleProtocol(kernel_handle, &li_guid, (void **)&li);
    if (li) {
        /*
         * Override DeviceHandle to the actual ESP where the kernel lives.
         * When LoadImage() is called with a buffer (NULL device path), the
         * kernel's DeviceHandle defaults to gImage (the USB bootloader device).
         * Setting it to the real SFS handle lets the EFI stub find initrd=
         * files on the correct partition.
         */
        EFI_HANDLE dev = get_device_handle(fs_ctx);
        if (dev) li->DeviceHandle = dev;

        /*
         * Build cmdline: the entry cmdline already contains "options" from
         * loader entries (including root=).  Append initrd=\path for any
         * initrd not already encoded there.
         */
        const char *cmd = entry->cmdline; /* may already have initrd= from loader entry */
        /* If entry->initrd is set but cmdline has no initrd=, prepend it */
        char full_cmd[512];
        int fc = 0;

        /* Check if cmdline already has initrd= */
        bool has_initrd = false;
        for (int j = 0; cmd[j]; j++) {
            if (cmd[j]=='i'&&cmd[j+1]=='n'&&cmd[j+2]=='i'&&cmd[j+3]=='t'&&
                cmd[j+4]=='r'&&cmd[j+5]=='d'&&cmd[j+6]=='=') {
                has_initrd = true; break;
            }
        }

        if (!has_initrd && entry->initrd[0]) {
            /* Prepend initrd=\path — convert forward slashes to backslashes */
            const char *pfx = "initrd=";
            for (int j = 0; pfx[j] && fc < 510; j++) full_cmd[fc++] = pfx[j];
            for (int j = 0; entry->initrd[j] && fc < 510; j++)
                full_cmd[fc++] = (entry->initrd[j] == '/') ? '\\' : entry->initrd[j];
            if (cmd[0]) full_cmd[fc++] = ' ';
        }
        /* Append original cmdline */
        for (int j = 0; cmd[j] && fc < 510; j++) full_cmd[fc++] = cmd[j];
        full_cmd[fc] = 0;

        /* Convert to CHAR16 LoadOptions */
        CHAR16 *wopt = NULL;
        if (!EFI_ERROR(gBS->AllocatePool(EfiLoaderData,
                                          (fc + 1) * sizeof(CHAR16),
                                          (void **)&wopt)) && wopt) {
            for (int j = 0; j <= fc; j++)
                wopt[j] = (CHAR16)(unsigned char)full_cmd[j];
            li->LoadOptions     = wopt;
            li->LoadOptionsSize = (UINT32)((fc + 1) * sizeof(CHAR16));
        }
    }

    /* Hand off to the kernel — never returns on success */
    gBS->StartImage(kernel_handle, NULL, NULL);

    /* StartImage returned → something went wrong; clean up */
    gBS->UnloadImage(kernel_handle);
    return 0;

#else /* BIOS path below */
    u64  ksize = 0, isize = 0;
    u8  *kbuf  = NULL;
    u8  *ibuf  = NULL;
    (void)fs_ctx;
    FatFile *kf = fat_open(entry->kernel);
    if (!kf) return 0;
    ksize = fat_file_size(kf);
    if (ksize > sizeof(linux_kernel_buf)) { fat_close(kf); return 0; }
    fat_read(kf, linux_kernel_buf, (u32)ksize);
    fat_close(kf);
    kbuf = linux_kernel_buf;

    if (entry->initrd[0]) {
        FatFile *rf = fat_open(entry->initrd);
        if (rf) {
            isize = fat_file_size(rf);
            if (isize <= sizeof(linux_initrd_buf)) {
                fat_read(rf, linux_initrd_buf, (u32)isize);
                ibuf = linux_initrd_buf;
            }
            fat_close(rf);
        }
    }

    if (!kbuf || ksize < 512) return 0;

    LinuxSetupHeader *hdr = (LinuxSetupHeader *)(kbuf + 0x1F1);
    if (hdr->header != LINUX_MAGIC) return 0;

    /* Number of setup sectors (minimum 4) */
    u32 setup_sects = hdr->setup_sects ? hdr->setup_sects : 4;
    u32 setup_size  = (setup_sects + 1) * 512;

    /* Copy setup code to 0x90000 */
    u8 *boot_params = (u8 *)(uintptr_t)BOOT_PARAMS_ADDR;
    for (u32 i = 0; i < 4096; i++) boot_params[i] = 0; /* Zero page */
    for (u32 i = 0; i < setup_size && i < ksize; i++)
        boot_params[i] = kbuf[i];

    /* Reload header pointer after copy */
    hdr = (LinuxSetupHeader *)(boot_params + 0x1F1);

    /* Fill in boot params */
    hdr->type_of_loader = LOADER_TYPE;
    hdr->loadflags     |= CAN_USE_HEAP;
    hdr->heap_end_ptr   = 0xFE00;
    hdr->ext_loader_ver = 0;

    /* Command line */
    char *cmdline = (char *)(uintptr_t)CMDLINE_ADDR;
    str_copy_n(cmdline, entry->cmdline, 2048);
    hdr->cmd_line_ptr   = (u32)CMDLINE_ADDR;

    /* Load protected-mode kernel at 1 MB */
    u32 pm_offset = setup_size;
    u32 pm_size   = (u32)(ksize - pm_offset);
    u8 *pm_dest   = (u8 *)(uintptr_t)KERNEL_ADDR;
    for (u32 i = 0; i < pm_size; i++) pm_dest[i] = kbuf[pm_offset + i];

    /* Initrd */
    if (ibuf && isize) {
        u64 initrd_addr = 0x02000000UL; /* 32 MB */
        u8 *idst = (u8 *)(uintptr_t)initrd_addr;
        for (u64 i = 0; i < isize; i++) idst[i] = ibuf[i];
        hdr->ramdisk_image = (u32)initrd_addr;
        hdr->ramdisk_size  = (u32)isize;
        info->initrd_addr  = initrd_addr;
        info->initrd_size  = isize;
    }

    /* Return entry point of 64-bit Linux kernel */
    return KERNEL_ADDR + 0x200; /* x86_64 entry at +0x200 from 1MB */
#endif
}

static void linux_boot(u64 entry_point, BootInfo *info) {
    (void)info;
#if defined(__x86_64__)
    /*
     * Linux x86_64 64-bit entry:
     *   RSI = pointer to boot_params (zero page) at 0x90000
     *   All other GP registers cleared
     */
    typedef void (*LinuxEntry)(void);
    LinuxEntry ep = (LinuxEntry)(uintptr_t)entry_point;

    __asm__ volatile (
        "xor %%rax, %%rax\n\t"
        "xor %%rbx, %%rbx\n\t"
        "xor %%rcx, %%rcx\n\t"
        "xor %%rdx, %%rdx\n\t"
        "xor %%rdi, %%rdi\n\t"
        "mov $0x90000, %%rsi\n\t"
        "jmp *%0\n\t"
        :: "r"(ep) : "memory"
    );
    while (1) __asm__ volatile ("hlt");
#elif defined(__aarch64__)
    /* Linux AArch64 boot protocol: X0 = device tree, others = 0 */
    typedef void (*AArch64Entry)(void *dtb, u64, u64, u64);
    AArch64Entry ep = (AArch64Entry)(uintptr_t)entry_point;
    ep(NULL, 0, 0, 0);
    while (1) __asm__ volatile ("wfi");
#endif
}

const OSLoader linux_loader = {
    .name     = "Linux bzImage",
    .can_load = linux_can_load,
    .load     = linux_load,
    .boot     = linux_boot,
};
