#pragma once
#include "../common/types.h"
#include "luks1.h"   /* reuse LuksReadFn */
#include "../crypto/cipher.h"

#define LUKS2_MAGIC1       "LUKS\xba\xbe"
#define LUKS2_MAGIC2       "SKUL\xba\xbe"
#define LUKS2_MAGIC_LEN    6
#define LUKS2_HEADER_SIZE  4096

/*
 * Try to unlock a LUKS2 volume.
 * Same calling convention as luks1_unlock.
 */
int luks2_unlock(LuksReadFn read_fn, void *read_ctx,
                 const u8 *passphrase, u32 plen,
                 u8 *master_key_out,
                 u32 *key_bytes_out,
                 u64 *payload_offset_out,
                 int *cipher_mode_out);

/* Returns 1 if buf[0..511] starts with LUKS2 header magic */
int luks2_probe(const u8 *buf);
