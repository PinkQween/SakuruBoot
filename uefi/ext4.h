/*
 * Minimal ext4 reader for SakuruBoot.
 * Scans ext4 partitions (via EFI Disk IO) for bootable kernels.
 *
 * Limitations:
 *  - Extent tree depth 0 and 1 only (sufficient for /boot kernels up to ~128 MB)
 *  - Direct blocks only for non-extent inodes (ext2/3 compat)
 *  - No htree / hash-tree directory support (linear scan only)
 *  - Read-only; no journaling
 */
#pragma once

#include "efi.h"
#include <stdbool.h>

/* Opaque volume handle */
typedef struct Ext4Vol Ext4Vol;

/*
 * Try to mount the partition identified by part_handle as ext4.
 * part_handle must expose EFI_BLOCK_IO_PROTOCOL + EFI_DISK_IO_PROTOCOL and
 * must be a logical partition (not a raw disk handle).
 * Returns NULL if not ext4 or on any error.
 */
Ext4Vol *ext4_mount(EFI_HANDLE part_handle);

/* Free resources allocated by ext4_mount. */
void ext4_unmount(Ext4Vol *vol);

/*
 * Read a file into EfiLoaderData pool memory.
 * The caller must FreePool the returned buffer.
 * Returns NULL on error (file not found, I/O error, etc.).
 */
void *ext4_read_file(Ext4Vol *vol, const char *path, UINTN *out_size);

/*
 * Return non-zero file size if path exists, 0 if not found.
 * Cheaper than ext4_read_file — only reads the inode.
 */
UINTN ext4_stat(Ext4Vol *vol, const char *path);

/*
 * Enumerate directory entries at path.
 * cb is called once per entry (skipping "." and "..").
 */
typedef void (*Ext4DirCb)(void *ctx, const char *name, bool is_dir);
void ext4_readdir(Ext4Vol *vol, const char *path, Ext4DirCb cb, void *ctx);
