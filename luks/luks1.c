/*
 * LUKS1 (on-disk format version 1) unlock logic.
 *
 * Spec: https://gitlab.com/cryptsetup/cryptsetup/-/wikis/LUKS-standard
 *
 * Algorithm:
 *   For each active key slot:
 *     1. Derive the slot key from the passphrase via PBKDF2(hash, password, slot_salt, slot_iter, key_bytes * stripes)
 *     2. Decrypt the AF-split key material stored at key_material_offset
 *     3. AF-merge (anti-forensic merge) to recover the candidate master key
 *     4. Verify: PBKDF2(hash, candidate_mk, mk_digest_salt, mk_digest_iter, 20) == mk_digest
 *     5. If verified, return the master key
 */

#include "luks1.h"
#include "../crypto/pbkdf2.h"
#include "../crypto/cipher.h"
#include "../crypto/sha.h"
#include <stddef.h>

static u16 be16r(const u8 *p){return((u16)p[0]<<8)|(u16)p[1];}
static u32 be32r(const u8 *p){return((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|(u32)p[3];}

int luks1_probe(const u8 *buf) {
    return buf[0]=='L'&&buf[1]=='U'&&buf[2]=='K'&&buf[3]=='S'&&
           buf[4]==0xba&&buf[5]==0xbe;
}

/* ------------------------------------------------------------------ */
/* AF (Anti-Forensic) merge                                            */
/* Reconstructs the master key from 'stripes' × key_bytes stripe data */
/* ------------------------------------------------------------------ */
static void af_xor(u8 *dst, const u8 *src, u32 len) {
    for(u32 i=0;i<len;i++) dst[i]^=src[i];
}

static void af_hash_sector(int hash_id, u32 sector, u32 key_bytes,
                            const u8 *data, u8 *out) {
    /* d || be32(sector) → hash */
    u8 tmp[256+4];
    for(u32 i=0;i<key_bytes;i++) tmp[i]=data[i];
    tmp[key_bytes+0]=(u8)(sector>>24);
    tmp[key_bytes+1]=(u8)(sector>>16);
    tmp[key_bytes+2]=(u8)(sector>>8);
    tmp[key_bytes+3]=(u8)(sector);
    hash_compute(hash_id, tmp, key_bytes+4, out);
}

static void af_merge(int hash_id, const u8 *split, u32 stripes,
                     u32 key_bytes, u8 *mk) {
    u8 d[64]={0};
    for(u32 s=0;s<stripes-1;s++){
        af_xor(d, split + s*key_bytes, key_bytes);
        af_hash_sector(hash_id, s, key_bytes, d, d);
    }
    af_xor(d, split + (stripes-1)*key_bytes, key_bytes);
    for(u32 i=0;i<key_bytes;i++) mk[i]=d[i];
}

/* ------------------------------------------------------------------ */
/* Decrypt key material                                                */
/* ------------------------------------------------------------------ */
static void decrypt_key_material(const char *cipher_name,
                                 const char *cipher_mode,
                                 const u8 *slot_key, u32 key_bytes,
                                 const u8 *encrypted, u8 *decrypted,
                                 u32 total_len) {
    int mode = cipher_mode_id(cipher_mode);
    (void)cipher_name; /* assume aes */

    if (mode == CIPHER_XTS) {
        XTS_CTX ctx;
        xts_init(&ctx, slot_key, (int)(key_bytes*2), 1);
        u32 sector_size = 512;
        for(u32 off=0;off<total_len;off+=sector_size){
            u32 chunk=(total_len-off<sector_size)?total_len-off:sector_size;
            /* Pad to 16-byte boundary for XTS */
            u32 aligned=(chunk+15)&~15u;
            u8 tmp[512]={0};
            for(u32 i=0;i<chunk;i++) tmp[i]=encrypted[off+i];
            xts_crypt(&ctx,(u64)(off/sector_size),tmp,tmp,aligned,1);
            for(u32 i=0;i<chunk;i++) decrypted[off+i]=tmp[i];
        }
    } else {
        CBC_CTX ctx;
        cbc_init(&ctx, slot_key, (int)key_bytes, mode, NULL, 0);
        for(u32 off=0;off<total_len;off+=512){
            u32 chunk=(total_len-off<512)?total_len-off:512;
            cbc_decrypt_sector(&ctx,(u64)(off/512),encrypted+off,decrypted+off,chunk);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Main unlock                                                         */
/* ------------------------------------------------------------------ */

/* 16 sectors = 8192 bytes scratch for key material read */
#define KM_READ_SECTORS 256  /* max sectors per read */
#define KM_BUF_SIZE     (KM_READ_SECTORS * 512)

/* Static scratch buffers (avoid large stack allocs in bootloader) */
static u8 s_km_enc[KM_BUF_SIZE];
static u8 s_km_dec[KM_BUF_SIZE];
static u8 s_slot_key[64];   /* derived slot key */

int luks1_unlock(LuksReadFn read_fn, void *read_ctx,
                 const u8 *passphrase, u32 plen,
                 u8 *master_key_out,
                 u32 *key_bytes_out,
                 u64 *payload_offset_out,
                 int *cipher_mode_out) {

    /* Read the header (sector 0) */
    u8 hdr_buf[512*4];
    if (read_fn(read_ctx, 0, hdr_buf, 4) != 0) return -1;

    if (!luks1_probe(hdr_buf)) return -1;

    Luks1Header *h = (Luks1Header *)hdr_buf;
    if (be16r((u8*)&h->version) != 1) return -1;

    u32 key_bytes = be32r((u8*)&h->key_bytes);
    if (key_bytes > 64) return -1; /* sanity */

    int hid = hash_id(h->hash_spec);
    if (hid < 0) hid = HASH_SHA1;

    /* Try each key slot */
    for (int slot = 0; slot < LUKS1_KEY_SLOTS; slot++) {
        Luks1KeySlot *ks = &h->key_slots[slot];
        if (be32r((u8*)&ks->active) != LUKS1_KEY_ENABLED) continue;

        u32 iters   = be32r((u8*)&ks->iterations);
        u32 stripes = be32r((u8*)&ks->stripes);
        u32 km_off  = be32r((u8*)&ks->key_material_offset); /* in sectors */
        u32 km_len  = key_bytes * stripes;

        /* Derive slot key via PBKDF2 */
        pbkdf2(hid, passphrase, plen,
               ks->salt, LUKS1_SALTSIZE,
               iters,
               s_slot_key, key_bytes);

        /* Read encrypted key material */
        u32 km_sectors = (km_len + 511) / 512;
        if (km_sectors > KM_READ_SECTORS) continue;

        if (read_fn(read_ctx, (u64)km_off, s_km_enc, km_sectors) != 0) continue;

        /* Decrypt key material */
        decrypt_key_material(h->cipher_name, h->cipher_mode,
                             s_slot_key, key_bytes,
                             s_km_enc, s_km_dec, km_len);

        /* AF merge to recover candidate master key */
        u8 candidate_mk[64];
        af_merge(hid, s_km_dec, stripes, key_bytes, candidate_mk);

        /* Verify: PBKDF2(hash, mk, mk_salt, mk_iter, 20) == mk_digest */
        u8 digest[20];
        pbkdf2(hid, candidate_mk, key_bytes,
               h->mk_digest_salt, LUKS1_SALTSIZE,
               be32r((u8*)&h->mk_digest_iter),
               digest, 20);

        int match = 1;
        for (int i = 0; i < 20; i++) if (digest[i] != h->mk_digest[i]) { match=0; break; }
        if (!match) continue;

        /* Success */
        for (u32 i = 0; i < key_bytes; i++) master_key_out[i] = candidate_mk[i];
        *key_bytes_out      = key_bytes;
        *payload_offset_out = (u64)be32r((u8*)&h->payload_offset);
        *cipher_mode_out    = cipher_mode_id(h->cipher_mode);
        return 0;
    }
    return -1;
}
