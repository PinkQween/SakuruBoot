/*
 * common/bootcount.h — Boot-attempt counter for automatic fallback
 *
 * Each time SakuruBoot starts it increments a per-entry counter in NVRAM.
 * When the OS initialises successfully it must call `sakuruboot-mark-good`
 * (or directly clear the counter with a firmware vendor command).
 * If the counter reaches BOOTCOUNT_MAX without being cleared, SakuruBoot
 * automatically falls back to the previous (known-good) entry.
 */
#pragma once

#ifndef SAKURU_HOST_TEST
#include "types.h"
#include "../uefi/efi.h"

#define BOOTCOUNT_MAX 3  /* Max failed boots before fallback */

/*
 * bootcount_increment(entry_index)
 *   Increment the failure counter for this entry and persist to NVRAM.
 */
void bootcount_increment(u32 entry_index);

/*
 * bootcount_get(entry_index) → current count (0 if never set)
 */
u32 bootcount_get(u32 entry_index);

/*
 * bootcount_clear(entry_index)
 *   Reset the counter to 0 (call after a successful OS handoff confirmation
 *   or from the OS itself via sakuruboot-mark-good utility).
 */
void bootcount_clear(u32 entry_index);

/*
 * bootcount_should_fallback(entry_index)
 *   Returns 1 if the entry has exceeded BOOTCOUNT_MAX consecutive failures.
 */
int bootcount_should_fallback(u32 entry_index);

#endif /* !SAKURU_HOST_TEST */
