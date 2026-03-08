/*
 * tests/mocks/efi_mock.h — Stub EFI types and globals for host-compiled tests.
 *
 * Include this BEFORE any SakuruBoot headers when building unit tests
 * with -DSAKURU_HOST_TEST.  It provides the bare minimum needed to
 * compile common/, crypto/, and luks/ without actual EFI firmware.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Primitive EFI types */
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef uintptr_t UINTN;
typedef intptr_t  INTN;
typedef uint16_t  CHAR16;
typedef void     *EFI_HANDLE;
typedef uint64_t  EFI_STATUS;
typedef uint8_t   BOOLEAN;

#define TRUE  1
#define FALSE 0

#define EFI_SUCCESS 0ULL
#define EFI_ERR(x) ((EFI_STATUS)(0x8000000000000000ULL | (x)))
#define EFI_ERROR(s) ((INTN)(s) < 0)

typedef struct { UINT32 Data1; UINT16 Data2; UINT16 Data3; UINT8 Data4[8]; } EFI_GUID;

#define EFI_GUID_INIT(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
    { (a),(b),(c),{(d0),(d1),(d2),(d3),(d4),(d5),(d6),(d7)} }

/* Stub gBS_alloc_pool / gBS_free_pool used by argon2.c */
static inline void *gBS_alloc_pool(uint32_t sz) { return malloc(sz); }
static inline void  gBS_free_pool(void *p)       { free(p); }

/* Provide a dummy EFIAPI so uefi headers compile without ms_abi */
#ifndef EFIAPI
#define EFIAPI
#endif
