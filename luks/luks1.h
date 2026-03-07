#pragma once
#include "../common/types.h"
#include "../crypto/cipher.h"

#define LUKS1_MAGIC        "LUKS\xba\xbe"
#define LUKS1_MAGIC_LEN    6
#define LUKS1_CIPHERNAME_L 32
#define LUKS1_CIPHERMODE_L 32
#define LUKS1_HASHSPEC_L   32
#define LUKS1_UUID_L       40
#define LUKS1_DIGESTSIZE   20
#define LUKS1_SALTSIZE     32
#define LUKS1_KEY_SLOTS    8
#define LUKS1_STRIPES      4000

#define LUKS1_KEY_ENABLED  0x00AC71F3u
#define LUKS1_KEY_DISABLED 0x0000DEADu

/* On-disk key slot descriptor (within LUKS1 header) */
typedef struct {
    u32 active;
    u32 iterations;
    u8  salt[LUKS1_SALTSIZE];
    u32 key_material_offset;  /* in 512-byte sectors */
    u32 stripes;
} __attribute__((packed)) Luks1KeySlot;

/* Full LUKS1 on-disk header (1024 bytes at sector 0) */
typedef struct {
    u8           magic[LUKS1_MAGIC_LEN];
    u16          version;
    char         cipher_name[LUKS1_CIPHERNAME_L];
    char         cipher_mode[LUKS1_CIPHERMODE_L];
    char         hash_spec  [LUKS1_HASHSPEC_L];
    u32          payload_offset;     /* in sectors */
    u32          key_bytes;          /* master key size */
    u8           mk_digest[LUKS1_DIGESTSIZE];
    u8           mk_digest_salt[LUKS1_SALTSIZE];
    u32          mk_digest_iter;
    char         uuid[LUKS1_UUID_L];
    Luks1KeySlot key_slots[LUKS1_KEY_SLOTS];
} __attribute__((packed)) Luks1Header;

/*
 * Try to unlock a LUKS1 volume.
 *
 * read_sectors(ctx, lba, buf, count) — reads 'count' 512-byte sectors
 *   starting at 'lba' into 'buf'. Returns 0 on success.
 *
 * passphrase/plen — the candidate passphrase.
 * master_key_out  — caller-provided buffer (at least 64 bytes) to receive
 *                   the decrypted master key.
 * key_bytes_out   — receives the actual master key length.
 * payload_offset_out — receives the payload offset in sectors.
 * cipher_mode_out    — receives CIPHER_* id.
 *
 * Returns 0 on success (valid passphrase), -1 on failure.
 */
typedef int (*LuksReadFn)(void *ctx, u64 lba, u8 *buf, u32 count);

int luks1_unlock(LuksReadFn read_fn, void *read_ctx,
                 const u8 *passphrase, u32 plen,
                 u8 *master_key_out,
                 u32 *key_bytes_out,
                 u64 *payload_offset_out,
                 int *cipher_mode_out);

/* Check if a buffer starts with a LUKS1 header magic */
int luks1_probe(const u8 *buf);
