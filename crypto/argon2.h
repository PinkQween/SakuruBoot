#pragma once
#include "../common/types.h"

/*
 * Argon2 — memory-hard password hashing (RFC 9106).
 * Supports Argon2d (type=0), Argon2i (type=1), Argon2id (type=2).
 *
 * All LUKS2 installs use Argon2id.
 *
 * IMPORTANT: This allocates memory_kb kilobytes via the provided alloc/free
 * callbacks (UEFI: AllocatePool / FreePool). For typical LUKS2 defaults
 * (memory = 1 MB) this is fine in UEFI, but ensure you have enough pool.
 */

#define ARGON2_TYPE_D   0
#define ARGON2_TYPE_I   1
#define ARGON2_TYPE_ID  2

typedef void *(*Argon2AllocFn)(u32 size);
typedef void  (*Argon2FreeFn) (void *ptr);

/*
 * Derive a key using Argon2.
 * password/plen  — passphrase
 * salt/slen      — salt (16 bytes for LUKS2)
 * time_cost      — number of passes (iterations)
 * memory_kb      — memory in kibibytes
 * parallelism    — degree of parallelism (lanes)
 * type           — ARGON2_TYPE_{D,I,ID}
 * out/out_len    — output key buffer
 * alloc/free     — memory allocator (pass EFI AllocatePool / FreePool wrappers)
 *
 * Returns 0 on success, -1 on memory allocation failure.
 */
int argon2(const u8 *password, u32 plen,
           const u8 *salt,     u32 slen,
           u32 time_cost, u32 memory_kb, u32 parallelism,
           int type,
           u8 *out, u32 out_len,
           Argon2AllocFn alloc, Argon2FreeFn freefn);
