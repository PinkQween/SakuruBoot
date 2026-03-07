# Crypto Internals

SakuruBoot ships a complete freestanding crypto stack.  
All primitives are implemented in C11 with no stdlib — no `malloc`, no `memcpy`, no `string.h`.

## Hash functions — `crypto/sha.c`

| Function | Algorithm | Output |
|----------|-----------|--------|
| `sha1()`   | SHA-1   | 20 bytes |
| `sha256()` | SHA-256 | 32 bytes |
| `sha512()` | SHA-512 | 64 bytes |

Used by HMAC and PBKDF2.

## HMAC — `crypto/hmac.c`

```
hmac_sha1(key, klen, msg, mlen, out)
hmac_sha256(key, klen, msg, mlen, out)
hmac_sha512(key, klen, msg, mlen, out)
```

RFC 2104 implementation. Used internally by PBKDF2.

## PBKDF2 — `crypto/pbkdf2.c`

```
pbkdf2_sha1(pass, plen, salt, slen, iter, out, dklen)
pbkdf2_sha256(pass, plen, salt, slen, iter, out, dklen)
pbkdf2_sha512(pass, plen, salt, slen, iter, out, dklen)
```

RFC 2898 / PKCS#5 v2.0. Used by LUKS1 and LUKS2 PBKDF2 keyslots.

## Blake2b — `crypto/blake2b.c`

```
blake2b(out, outlen, in, inlen, key, keylen)
```

RFC 7693. Used by LUKS2 key digest verification.

## Argon2 — `crypto/argon2.c`

```
argon2id(pass, plen, salt, slen, t_cost, m_cost, parallelism, out, outlen)
argon2i(...)
argon2d(...)
```

RFC 9106. Memory allocated via `gBS_alloc_pool` (UEFI AllocatePool).  
Default LUKS2 parameters: `m_cost = 65536` KiB — ensure 64 MB UEFI pool is available.

## AES — `crypto/aes.c`

Table-based AES-128 and AES-256 (FIPS 197). Provides key schedule and single-block encrypt/decrypt.

## Cipher modes — `crypto/cipher.c`

| Mode | LUKS name | Description |
|------|-----------|-------------|
| XTS-AES | `aes-xts-plain64` | IEEE 1619. Two AES keys; tweak per sector. |
| CBC-plain | `aes-cbc-plain` | IV = sector number (32-bit LE). |
| CBC-plain64 | `aes-cbc-plain64` | IV = sector number (64-bit LE). |
| CBC-ESSIV | `aes-cbc-essiv:sha256` | IV = AES(SHA256(key), sector). |

The cipher spec string from the LUKS header (`"aes-xts-plain64"`) is parsed  
automatically — no configuration required.

## Source files

```
crypto/
├── sha.c / sha.h        — SHA-1, SHA-256, SHA-512
├── hmac.c / hmac.h      — HMAC-SHA{1,256,512}
├── pbkdf2.c / pbkdf2.h  — PBKDF2
├── blake2b.c / blake2b.h — Blake2b
├── argon2.c / argon2.h  — Argon2id/i/d
├── aes.c / aes.h        — AES block cipher
└── cipher.c / cipher.h  — XTS, CBC-plain, CBC-ESSIV
```
