/*
 * common/loader_entry.h — systemd-boot Type 1 Loader Entry spec parser
 *
 * Scans the ESP's /loader/entries/ directory for *.conf files and
 * merges any found entries into the live BootConfig.  This gives
 * SakuruBoot compatibility with tools like kernel-install that write
 * Type 1 entries (e.g. /loader/entries/linux-6.12.conf).
 *
 * Format of each .conf file (subset we support):
 *   title   <display name>
 *   linux   <absolute path to kernel>
 *   initrd  <absolute path to initrd>
 *   options <kernel cmdline>
 */
#pragma once

#ifndef SAKURU_HOST_TEST
#include "types.h"
#include "config.h"
#include "../uefi/efi.h"

/*
 * loader_entry_scan(root_dir, cfg)
 *   Open /loader/entries/ on root_dir (an EFI_FILE_PROTOCOL*), parse every
 *   *.conf file found, and append resulting entries to cfg.
 *   Silently skips unreadable files.  Stops at MAX_ENTRIES.
 */
void loader_entry_scan(void *root_dir, BootConfig *cfg);

#endif /* !SAKURU_HOST_TEST */
