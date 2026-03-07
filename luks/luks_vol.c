#include "luks_vol.h"
#include "luks1.h"
#include "luks2.h"
#include "../crypto/cipher.h"
#include <stddef.h>

/* Provided by uefi_loader.c */
extern void *gBS_alloc_pool(u32 size);
extern void  gBS_free_pool(void *p);

/* ------------------------------------------------------------------ */
/* Volume struct                                                       */
/* ------------------------------------------------------------------ */
struct LuksVol {
    LuksReadFn  read_fn;
    void       *read_ctx;
    u64         payload_lba;    /* first sector of payload */
    u32         sector_size;    /* payload sector size in bytes */
    int         cipher_mode;
    /* One of: xts or cbc */
    XTS_CTX     xts;
    CBC_CTX     cbc;
    u8          decrypt_buf[4096]; /* one sector decrypt scratch */
};

/* ------------------------------------------------------------------ */
LuksVol *luks_vol_open(LuksReadFn read_fn, void *read_ctx,
                       const u8 *master_key, u32 key_bytes,
                       u64 payload_offset_sectors,
                       int cipher_mode,
                       u32 sector_size) {
    LuksVol *v = (LuksVol *)gBS_alloc_pool(sizeof(LuksVol));
    if (!v) return NULL;
    /* memset */
    u8 *vb = (u8*)v;
    for (u32 i=0;i<sizeof(LuksVol);i++) vb[i]=0;

    v->read_fn      = read_fn;
    v->read_ctx     = read_ctx;
    v->payload_lba  = payload_offset_sectors;
    v->sector_size  = (sector_size >= 512) ? sector_size : 512;
    v->cipher_mode  = cipher_mode;

    if (cipher_mode == CIPHER_XTS) {
        xts_init(&v->xts, master_key, (int)key_bytes * 2, 1);
    } else {
        cbc_init(&v->cbc, master_key, (int)key_bytes, cipher_mode,
                 master_key, (int)key_bytes);
    }
    return v;
}

int luks_vol_read(LuksVol *vol, u64 lba, u8 *buf, u32 count) {
    u32 secs_per_payload = vol->sector_size / 512;
    if (secs_per_payload == 0) secs_per_payload = 1;

    for (u32 i = 0; i < count; ) {
        /* Map logical 512-byte sectors to payload sectors */
        u64 payload_sec = lba / secs_per_payload;
        u32 sec_off     = (u32)(lba % secs_per_payload); /* offset within payload sector */

        /* Read one encrypted payload sector (512 or 4096 bytes) */
        u64 device_lba = vol->payload_lba + payload_sec * secs_per_payload;
        /* We need vol->sector_size / 512 device sectors */
        u32 dev_secs = secs_per_payload;
        u8 enc_buf[4096];
        if (dev_secs * 512 > sizeof(enc_buf)) dev_secs = sizeof(enc_buf)/512;
        if (vol->read_fn(vol->read_ctx, device_lba, enc_buf, dev_secs) != 0)
            return -1;

        /* Decrypt the payload sector */
        if (vol->cipher_mode == CIPHER_XTS) {
            xts_crypt(&vol->xts, payload_sec, enc_buf, vol->decrypt_buf,
                      vol->sector_size, 1);
        } else {
            cbc_decrypt_sector(&vol->cbc, payload_sec,
                               enc_buf, vol->decrypt_buf, vol->sector_size);
        }

        /* Copy the requested 512-byte block(s) from within this payload sector */
        u32 copy = count - i;
        u32 avail = secs_per_payload - sec_off;
        if (copy > avail) copy = avail;
        for (u32 j=0;j<copy*512;j++) buf[(i+j/512)*512+(j%512)] =
            vol->decrypt_buf[sec_off*512 + j];

        i    += copy;
        lba  += copy;
    }
    return 0;
}

void luks_vol_close(LuksVol *vol) {
    if (!vol) return;
    /* Wipe key material before freeing */
    u8 *b = (u8*)vol;
    for (u32 i=0;i<sizeof(LuksVol);i++) b[i]=0;
    gBS_free_pool(vol);
}

/* ------------------------------------------------------------------ */
LuksVol *luks_open(LuksReadFn read_fn, void *read_ctx,
                   const u8 *passphrase, u32 plen) {
    /* Read first 8 sectors to probe */
    u8 probe[512*8];
    if (read_fn(read_ctx, 0, probe, 8) != 0) return NULL;

    u8  master_key[64];
    u32 key_bytes = 0;
    u64 payload_offset = 0;
    int cipher_mode = CIPHER_XTS;
    int ok = -1;

    if (luks2_probe(probe)) {
        ok = luks2_unlock(read_fn, read_ctx, passphrase, plen,
                          master_key, &key_bytes, &payload_offset, &cipher_mode);
    } else if (luks1_probe(probe)) {
        ok = luks1_unlock(read_fn, read_ctx, passphrase, plen,
                          master_key, &key_bytes, &payload_offset, &cipher_mode);
    }

    if (ok != 0) return NULL;

    LuksVol *vol = luks_vol_open(read_fn, read_ctx,
                                 master_key, key_bytes,
                                 payload_offset, cipher_mode, 512);
    /* Wipe master key from stack */
    for (int i=0;i<64;i++) master_key[i]=0;
    return vol;
}

/* ------------------------------------------------------------------ */
/* UEFI EFI_BLOCK_IO_PROTOCOL adapter                                 */
/* ------------------------------------------------------------------ */

/*
 * EFI_BLOCK_IO_PROTOCOL minimal definition (enough to call ReadBlocks).
 * Matches the UEFI spec layout exactly.
 */
typedef struct {
    u64    Revision;
    void  *Media;
    void  *Reset;
    /* ReadBlocks(This, MediaId, LBA, BufferSize, Buffer) */
    u64  (*ReadBlocks)(void *This, u32 MediaId, u64 LBA,
                        u32 BufferSize, void *Buffer);
    void  *WriteBlocks;
    void  *FlushBlocks;
} EfiBlockIO;

typedef struct {
    EfiBlockIO *bio;
    u32         media_id;
} EfiBioCtx;

static int efi_bio_read(void *ctx, u64 lba, u8 *buf, u32 count) {
    EfiBioCtx *c = (EfiBioCtx *)ctx;
    u64 status = c->bio->ReadBlocks(c->bio, c->media_id, lba,
                                     (u32)(count * 512), buf);
    return (status == 0) ? 0 : -1;
}

/* We need the media_id from the Bio->Media struct.
 * EFI_BLOCK_IO_MEDIA: first field is MediaId (u32). */
typedef struct { u32 MediaId; } EfiBlockMedia;

static EfiBioCtx s_efi_bio_ctx;

void *luks_open_efi(void *bio, const u8 *passphrase, u32 plen) {
    EfiBlockIO *b = (EfiBlockIO *)bio;
    EfiBlockMedia *m = (EfiBlockMedia *)b->Media;
    s_efi_bio_ctx.bio      = b;
    s_efi_bio_ctx.media_id = m->MediaId;
    return luks_open(efi_bio_read, &s_efi_bio_ctx, passphrase, plen);
}
