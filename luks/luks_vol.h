#pragma once
#include "../common/types.h"
#include "luks1.h"
#include "../crypto/cipher.h"

/*
 * Unified LUKS handle — wraps both LUKS1 and LUKS2.
 * After successful unlock, use luks_vol_read() to read plaintext sectors
 * from the encrypted payload.
 */
typedef struct LuksVol LuksVol;

/*
 * Detect LUKS version, prompt via luks_unlock, and return a mounted volume.
 *
 * read_fn / read_ctx : block device reader (512-byte sectors)
 * master_key / key_bytes : the unlocked master key
 * payload_offset : first payload sector
 * cipher_mode    : CIPHER_* constant
 * sector_size    : payload sector size in bytes (usually 512 or 4096)
 * alloc / free   : memory allocator (UEFI pool)
 *
 * Returns NULL on failure.
 */
LuksVol *luks_vol_open(LuksReadFn read_fn, void *read_ctx,
                       const u8 *master_key, u32 key_bytes,
                       u64 payload_offset_sectors,
                       int cipher_mode,
                       u32 sector_size);

/*
 * Read 'count' 512-byte sectors starting at logical block address 'lba'
 * (relative to the start of the decrypted payload) into buf.
 * Returns 0 on success, -1 on error.
 */
int luks_vol_read(LuksVol *vol, u64 lba, u8 *buf, u32 count);

void luks_vol_close(LuksVol *vol);

/*
 * High-level: auto-detect LUKS1 or LUKS2, try passphrase, open volume.
 * Returns NULL if not LUKS or passphrase wrong.
 */
LuksVol *luks_open(LuksReadFn read_fn, void *read_ctx,
                   const u8 *passphrase, u32 plen);

/*
 * UEFI-specific: open a LUKS volume directly from an EFI_BLOCK_IO_PROTOCOL.
 * bio must be a pointer to an EFI_BLOCK_IO_PROTOCOL; the function wraps it
 * in a LuksReadFn and calls luks_open.
 * Returns NULL if not LUKS or passphrase wrong.
 */
void *luks_open_efi(void *bio, const u8 *passphrase, u32 plen);
