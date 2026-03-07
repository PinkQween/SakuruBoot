/*
 * SakuruBoot — x86_64 UEFI entry point
 */

#include "../efi.h"
#include "../uefi_loader.h"

/*
 * Stub for Windows x64 stack probe (___chkstk_ms).
 * mingw-w64 may emit calls to this when allocating large stack frames.
 * In a freestanding EFI environment we simply return.
 */
void __attribute__((naked)) ___chkstk_ms(void) {
    __asm__ volatile ("ret");
}

/* EFI application entry point — Microsoft x64 ABI */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
                            EFI_SYSTEM_TABLE *SystemTable) {
    return uefi_main(ImageHandle, SystemTable);
}
