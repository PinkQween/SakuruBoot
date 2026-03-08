/*
 * common/bootcount.c — Boot-attempt counter for automatic fallback
 */

#ifndef SAKURU_HOST_TEST

#include "bootcount.h"
#include "efivar.h"
#include "types.h"
#include "../uefi/efi.h"

extern EFI_SYSTEM_TABLE  *gST;

typedef EFI_STATUS (EFIAPI *GetVariableFn)(CHAR16 *, EFI_GUID *, UINT32 *,
                                            UINTN *, void *);
typedef EFI_STATUS (EFIAPI *SetVariableFn)(CHAR16 *, EFI_GUID *, UINT32,
                                            UINTN, void *);
#define rt_get_var ((GetVariableFn)(gST->RuntimeServices->GetVariable))
#define rt_set_var ((SetVariableFn)(gST->RuntimeServices->SetVariable))

static EFI_GUID s_vendor_guid = SAKURU_VENDOR_GUID;

/* Build variable name "SakuruBC<index>" as UCS-2 (max index 99) */
static void build_var_name(CHAR16 *buf, u32 index) {
    const CHAR16 prefix[] = {
        'S','a','k','u','r','u','B','C', 0
    };
    int i = 0;
    while (prefix[i]) { buf[i] = prefix[i]; i++; }
    /* Append decimal digits */
    if (index >= 10) {
        buf[i++] = (CHAR16)('0' + (index / 10) % 10);
    }
    buf[i++] = (CHAR16)('0' + index % 10);
    buf[i]   = 0;
}

u32 bootcount_get(u32 entry_index) {
    CHAR16 name[16];
    build_var_name(name, entry_index);
    UINTN   data_size = sizeof(u32);
    UINT32  attrs     = 0;
    u32     val       = 0;
    EFI_STATUS s = rt_get_var(name, &s_vendor_guid,
                               &attrs, &data_size, &val);
    if (s != 0) return 0;
    return val;
}

void bootcount_increment(u32 entry_index) {
    u32 val = bootcount_get(entry_index) + 1;
    CHAR16 name[16];
    build_var_name(name, entry_index);
    rt_set_var(name, &s_vendor_guid,
               SAKURU_VAR_ATTR, sizeof(u32), &val);
}

void bootcount_clear(u32 entry_index) {
    u32 val = 0;
    CHAR16 name[16];
    build_var_name(name, entry_index);
    rt_set_var(name, &s_vendor_guid,
               SAKURU_VAR_ATTR, sizeof(u32), &val);
}

int bootcount_should_fallback(u32 entry_index) {
    return bootcount_get(entry_index) >= BOOTCOUNT_MAX;
}

#endif /* !SAKURU_HOST_TEST */
