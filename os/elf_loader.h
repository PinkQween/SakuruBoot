#pragma once

#include "../common/types.h"

/*
 * ELF64 parser for SakuruBoot.
 *
 * Parses an ELF64 binary from a memory buffer and loads its
 * PT_LOAD segments into memory at their specified physical addresses.
 * Returns the kernel entry point, or 0 on failure.
 */

/* ELF64 header and program header types */
#define ELFMAG0     0x7F
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define ET_DYN      3
#define EM_X86_64   62
#define EM_AARCH64  183
#define PT_LOAD     1

typedef struct __attribute__((packed)) {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} Elf64_Ehdr;

typedef struct __attribute__((packed)) {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} Elf64_Phdr;

/*
 * Load an ELF64 image from buf (size bytes) into memory.
 * Returns entry point virtual address, or 0 on error.
 */
u64 elf64_load(const u8 *buf, u64 size);

/* Validate ELF64 header. Returns true if valid. */
bool elf64_valid(const u8 *buf, u64 size);
