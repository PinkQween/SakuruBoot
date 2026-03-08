/*
 * common/mem.c — Freestanding memset / memcpy / memmove
 *
 * GCC may emit calls to these even with -ffreestanding when it decides
 * not to inline them (common on AArch64 for struct/array copies).
 * We provide simple implementations so that -nostdlib links succeed.
 */

#include "types.h"

void *memset(void *dst, int c, __SIZE_TYPE__ n) {
    u8 *d = (u8 *)dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, __SIZE_TYPE__ n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, __SIZE_TYPE__ n) {
    const u8 *p = (const u8 *)a;
    const u8 *q = (const u8 *)b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}
