#pragma once

#include "../../common/types.h"

/*
 * Minimal FAT32 read-only driver for SakuruBoot Stage 2.
 *
 * Usage:
 *   fat_init(lba_of_partition_start) — must be called first.
 *   fat_open(path)                   — returns a file handle or NULL.
 *   fat_read(handle, buf, len)       — reads up to len bytes.
 *   fat_close(handle)                — release handle.
 *   fat_file_size(handle)            — returns file size in bytes.
 */

#define FAT_MAX_PATH  256

typedef struct FatFile FatFile;

int      fat_init(u64 part_lba);        /* Returns 0 on success         */
FatFile *fat_open(const char *path);    /* path = "/dir/file.ext"       */
u32      fat_read(FatFile *f, void *buf, u32 len);
void     fat_close(FatFile *f);
u32      fat_file_size(FatFile *f);

/* Enumerate directory entries.  cb is called once per entry (excluding
 * . and ..).  Returns true on success, false if dir_path not found. */
typedef void (*FatDirCb)(const char *name, bool is_dir, void *ctx);
bool     fat_readdir(const char *dir_path, FatDirCb cb, void *ctx);
