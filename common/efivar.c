/*
 * common/efivar.c — EFI NVRAM variable helpers
 */

#ifndef SAKURU_HOST_TEST

#include "efivar.h"
#include "types.h"
#include "../uefi/efi.h"

/* Globals provided by uefi_loader.c */
extern EFI_SYSTEM_TABLE  *gST;

typedef EFI_STATUS (EFIAPI *GetVariableFn)(CHAR16 *, EFI_GUID *, UINT32 *,
                                            UINTN *, void *);
typedef EFI_STATUS (EFIAPI *SetVariableFn)(CHAR16 *, EFI_GUID *, UINT32,
                                            UINTN, void *);
#define rt_get_var ((GetVariableFn)(gST->RuntimeServices->GetVariable))
#define rt_set_var ((SetVariableFn)(gST->RuntimeServices->SetVariable))

/* Compile-time UCS-2 literal helper */
static const CHAR16 LAST_ENTRY_VAR[] = {
    'S','a','k','u','r','u','L','a','s','t','E','n','t','r','y', 0
};

static EFI_GUID s_vendor_guid = SAKURU_VENDOR_GUID;

void efivar_save_last_entry(u32 index) {
    rt_set_var((CHAR16 *)LAST_ENTRY_VAR, &s_vendor_guid,
               SAKURU_VAR_ATTR, sizeof(u32), &index);
}

int efivar_load_last_entry(u32 *out_index) {
    UINTN   data_size = sizeof(u32);
    UINT32  attrs     = 0;
    u32     value     = 0;
    EFI_STATUS s = rt_get_var((CHAR16 *)LAST_ENTRY_VAR, &s_vendor_guid,
                               &attrs, &data_size, &value);
    if (s != 0 || data_size != sizeof(u32)) return -1;
    *out_index = value;
    return 0;
}

void efivar_delete(const CHAR16 *name16) {
    /* Setting DataSize=0 deletes the variable per UEFI spec 2.10 §8.2.3 */
    rt_set_var((CHAR16 *)name16, &s_vendor_guid, 0, 0, NULL);
}

#endif /* !SAKURU_HOST_TEST */
