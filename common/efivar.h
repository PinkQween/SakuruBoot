/*
 * common/efivar.h — EFI NVRAM variable helpers (UEFI builds only)
 *
 * Provides load/save of per-boot-entry index into a private EFI variable
 * so SakuruBoot can remember the last user selection across reboots.
 */
#pragma once

#ifndef SAKURU_HOST_TEST
#include "types.h"
#include "../uefi/efi.h"

/* Private GUID for all SakuruBoot NVRAM variables:
 * {c1a55b20-7f4a-11ef-8a2b-0800200c9a66} */
#define SAKURU_VENDOR_GUID \
    EFI_GUID_INIT(0xc1a55b20, 0x7f4a, 0x11ef, \
                  0x8a, 0x2b, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66)

/* Variable attribute flags (NV + BS) */
#define SAKURU_VAR_ATTR (0x00000001U | 0x00000002U)

/*
 * efivar_save_last_entry(index)
 *   Persist the selected boot-entry index to NVRAM.
 *   Silently ignores errors (best-effort).
 */
void efivar_save_last_entry(u32 index);

/*
 * efivar_load_last_entry(out_index)
 *   Read the previously saved entry index.
 *   Returns 0 on success, -1 if the variable doesn't exist or is corrupt.
 */
int efivar_load_last_entry(u32 *out_index);

/*
 * efivar_delete(name16)
 *   Delete a SakuruBoot NVRAM variable by UCS-2 name.
 */
void efivar_delete(const CHAR16 *name16);

#endif /* !SAKURU_HOST_TEST */
