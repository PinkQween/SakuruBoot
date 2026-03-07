#pragma once

#include "efi.h"
#include "../common/config.h"
#include "../os/os_loader.h"

/* Called from the arch-specific EFI entry point */
EFI_STATUS uefi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st);

/*
 * Global UEFI handles — set by uefi_main, used by OS loaders.
 */
extern EFI_SYSTEM_TABLE  *gST;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_HANDLE         gImage;

/*
 * Return the underlying EFI device handle for a given fs_ctx.
 * For FAT volumes this is the SFS handle (lets the kernel EFI stub resolve
 * initrd= paths on the correct device).  Returns NULL for ext4 volumes.
 */
EFI_HANDLE get_device_handle(void *fs_ctx);

/*
 * Read a file from the boot filesystem.
 * root: EFI_FILE_PROTOCOL* for the volume root.
 * path: ASCII path (e.g. "/boot/kernel.elf").
 * out_size: set to file size on success.
 * Returns a pool-allocated buffer (caller must not free before ExitBootServices),
 * or NULL on error.
 */
char *read_file(void *root, const char *path, UINTN *out_size);
