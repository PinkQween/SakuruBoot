/*
 * SakuruBoot — AArch64 UEFI entry point
 *
 * Compiled as a PE32+ application targeting aarch64.
 * Build with:
 *   aarch64-w64-mingw32-gcc -ffreestanding -fno-stack-protector \
 *     -nostdlib -Wl,--subsystem,10 ...
 *
 * On AArch64, UEFI uses the standard AAPCS64 calling convention,
 * so no special attribute is needed.
 */

#include "../efi.h"
#include "../uefi_loader.h"

EFI_STATUS efi_main(EFI_HANDLE ImageHandle,
                    EFI_SYSTEM_TABLE *SystemTable) {
    return uefi_main(ImageHandle, SystemTable);
}
