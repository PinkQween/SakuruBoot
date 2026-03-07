/*
 * ELF64 loader for SakuruBoot.
 * Loads PT_LOAD segments from an in-memory ELF buffer into their
 * specified physical addresses.
 */

#include "elf_loader.h"

/* memset / memcpy implementations (no libc available) */
static void mem_set(void *dst, u8 val, u64 len) {
    u8 *p = (u8 *)dst;
    for (u64 i = 0; i < len; i++) p[i] = val;
}

static void mem_copy(void *dst, const void *src, u64 len) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    for (u64 i = 0; i < len; i++) d[i] = s[i];
}

bool elf64_valid(const u8 *buf, u64 size) {
    if (size < sizeof(Elf64_Ehdr)) return false;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)buf;

    if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3)
        return false;

    if (eh->e_ident[4] != ELFCLASS64)  return false; /* Must be 64-bit  */
    if (eh->e_ident[5] != ELFDATA2LSB) return false; /* Must be LE      */
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) return false;

#if defined(__x86_64__)
    if (eh->e_machine != EM_X86_64)  return false;
#elif defined(__aarch64__)
    if (eh->e_machine != EM_AARCH64) return false;
#endif

    return true;
}

u64 elf64_load(const u8 *buf, u64 size) {
    if (!elf64_valid(buf, size)) return 0;

    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)buf;

    /* Walk program headers */
    for (u16 i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)
            (buf + eh->e_phoff + (u64)i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0)      continue;

        /* Destination: physical address specified in the program header */
        u8 *dest = (u8 *)(uintptr_t)ph->p_paddr;

        /* Copy file bytes */
        if (ph->p_filesz)
            mem_copy(dest, buf + ph->p_offset, ph->p_filesz);

        /* Zero the BSS portion (p_memsz > p_filesz) */
        if (ph->p_memsz > ph->p_filesz)
            mem_set(dest + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
    }

    return eh->e_entry;
}
