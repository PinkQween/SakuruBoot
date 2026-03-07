/*
 * ViOS kernel loader for SakuruBoot.
 *
 * ViOS kernels are plain ELF64 binaries.  The bootloader reads the
 * kernel file, validates the ELF header, loads each PT_LOAD segment
 * to its physical address, and returns the entry point.
 *
 * Boot protocol:
 *   - Architecture: x86_64 or AArch64
 *   - Entry called with: entry_point(BootInfo *info)
 *   - CPU is in 64-bit long mode / AArch64 EL1
 *   - Interrupts disabled, paging enabled (identity map or UEFI map)
 */

#include "os_loader.h"
#include "elf_loader.h"

/* Platform file-read helper — resolved at compile time */
#ifdef SAKURU_UEFI
#  include "../uefi/efi.h"
/* gBS from uefi_loader.c — needed for AllocatePages */
extern EFI_BOOT_SERVICES *gBS;

static u8 *read_kernel_uefi(const BootEntry *entry, u64 *out_size,
                              void *fs_ctx) {
    /* fs_ctx is EFI_FILE_PROTOCOL* (root directory) */
    /* Reuse uefi_loader's read_file — declare extern */
    extern char *read_file(void *root, const char *path, UINTN *out);
    UINTN sz = 0;
    char *buf = read_file(fs_ctx, entry->kernel, &sz);
    *out_size = sz;
    return (u8 *)buf;
}

/*
 * Reserve every PT_LOAD segment's physical pages via AllocatePages so that
 * UEFI's own AllocatePool (called inside exit_boot_services for the memory
 * map) cannot reclaim and overwrite the loaded kernel code.
 */
static void vios_reserve_elf_pages(const u8 *buf, u64 size) {
    if (size < sizeof(Elf64_Ehdr)) return;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)buf;
    if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3) return;

    for (u16 i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)
            (buf + eh->e_phoff + (u64)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD || !ph->p_memsz) continue;
        EFI_PHYSICAL_ADDRESS pa = ph->p_paddr;
        UINTN pages = (UINTN)((ph->p_memsz + 0xFFFu) >> 12);
        /* Ignore failures — page may already be allocated or firmware-reserved */
        gBS->AllocatePages(AllocateAddress, EfiLoaderCode, pages, &pa);
    }
}
#else
/* BIOS path: use FAT32 driver */
#  include "../bios/stage2/fat.h"
static u8 *read_kernel_bios(const BootEntry *entry, u64 *out_size) {
    FatFile *f = fat_open(entry->kernel);
    if (!f) return NULL;

    u32 sz = fat_file_size(f);
    /* Allocate below 16 MB for safety */
    static u8 kernel_buf[8 * 1024 * 1024]; /* 8 MB static buffer */
    if (sz > sizeof(kernel_buf)) { fat_close(f); return NULL; }

    fat_read(f, kernel_buf, sz);
    fat_close(f);
    *out_size = sz;
    return kernel_buf;
}
#endif

/* ------------------------------------------------------------------ */
static bool vios_can_load(const BootEntry *entry) {
    return entry->type == OS_TYPE_ELF64;
}

static u64 vios_load(const BootEntry *entry, BootInfo *info, void *fs_ctx) {
    (void)info;

    u64  size = 0;
    u8  *buf  = NULL;

#ifdef SAKURU_UEFI
    buf = read_kernel_uefi(entry, &size, fs_ctx);
#else
    (void)fs_ctx;
    buf = read_kernel_bios(entry, &size);
#endif

    if (!buf || !size) return 0;

#ifdef SAKURU_UEFI
    vios_reserve_elf_pages(buf, size);
#endif

    return elf64_load(buf, size);
}

static void vios_boot(u64 entry_point, BootInfo *info) {
#if defined(__x86_64__)
    /* UEFI is compiled with MS ABI (RCX/RDX args), but ViOS kernel uses
     * System V ABI (RDI first arg). Move info into RDI explicitly. */
    __asm__ volatile (
        "movq %1, %%rdi\n\t"
        "jmpq *%%rax"
        :
        : "a"(entry_point), "r"((u64)(uintptr_t)info)
        : "rdi", "memory"
    );
#elif defined(__aarch64__)
    /* UEFI and ViOS kernel both use AAPCS64 — plain function call is correct. */
    typedef void (*KernelEntry)(BootInfo *);
    ((KernelEntry)(uintptr_t)entry_point)(info);
#endif
}

const OSLoader vios_loader = {
    .name      = "ViOS ELF64",
    .can_load  = vios_can_load,
    .load      = vios_load,
    .boot      = vios_boot,
};
