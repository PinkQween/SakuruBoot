# UEFI Loader

The UEFI loader (`uefi/uefi_loader.c`) is the primary entry point for modern UEFI firmware.  
It implements the full boot flow: config parsing → menu → LUKS unlock → kernel handoff.

## Entry point

```c
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);
```

Standard UEFI application entry. Registered as `BOOTX64.EFI` / `BOOTAA64.EFI`.

## Boot flow

```
efi_main
 └─ parse_config()          // read sakuru.cfg from ESP root
     └─ show_menu()         // interactive OS selection
         └─ [if encrypted]
             └─ luks_open_efi()   // LUKS passphrase prompt + unlock
         └─ find_loader()         // locate kernel on ext4 or FAT
         └─ load_kernel()         // read kernel + initrd into memory
         └─ efi_start_image()     // handoff to Linux EFI stub
```

## Memory management

Two helpers wrap UEFI pool allocation:

```c
void *gBS_alloc_pool(usize size);
void  gBS_free_pool(void *ptr);
```

These are used by Argon2 (which needs up to 64 MB for default LUKS2 parameters)  
and by the LUKS volume handle.

## Filesystem support

| Filesystem | Source | Notes |
|------------|--------|-------|
| ext4 | `uefi/ext4.c` | LUKS-transparent read path |
| FAT12/16/32 | UEFI firmware | Via `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` |

The ext4 reader gains LUKS support via an optional `luks_vol` field in `Ext4Vol`.  
When set, `read_bytes` routes I/O through `luks_vol_read` for on-the-fly decryption.

## Console UI

- Menu: arrow key selection, timeout countdown, Enter to boot.
- Passphrase: masked `*` input from `common/passphrase.c`.
- Error messages printed to UEFI `ConOut`.

## Architecture notes

The x86_64 target produces a **PE32+ binary** directly via `x86_64-w64-mingw32-gcc`.  
The AArch64 target produces an **ELF** then strips to a flat binary with `objcopy`.  
Both are valid UEFI applications loadable by any compliant firmware.
