# ── BIOS build target ────────────────────────────────────────────────────
cmake_minimum_required(VERSION 3.22)

# ── Stage 1: MBR (NASM → flat binary) ──────────────────────────────────
if(SAKURU_NASM)
    add_custom_command(
        OUTPUT  ${CMAKE_BINARY_DIR}/mbr.bin
        COMMAND ${SAKURU_NASM} -f bin
                -o ${CMAKE_BINARY_DIR}/mbr.bin
                ${CMAKE_SOURCE_DIR}/bios/stage1/mbr.asm
        DEPENDS ${CMAKE_SOURCE_DIR}/bios/stage1/mbr.asm
        COMMENT "Assembling MBR stage1")
    add_custom_target(mbr ALL DEPENDS ${CMAKE_BINARY_DIR}/mbr.bin)
endif()

# ── Stage 2: C + ASM entry → ELF → flat binary ──────────────────────────
set(BIOS_STAGE2_SOURCES
    ${CMAKE_SOURCE_DIR}/bios/stage2/main.c
    ${CMAKE_SOURCE_DIR}/bios/stage2/disk.c
    ${CMAKE_SOURCE_DIR}/bios/stage2/fat.c
    ${CMAKE_SOURCE_DIR}/common/config.c
    ${CMAKE_SOURCE_DIR}/common/menu.c
    ${CMAKE_SOURCE_DIR}/os/elf_loader.c
    ${CMAKE_SOURCE_DIR}/os/vios.c
    ${CMAKE_SOURCE_DIR}/os/linux.c
    ${CMAKE_SOURCE_DIR}/os/windows.c)

# ASM entry object (64-bit ELF)
if(SAKURU_NASM)
    add_custom_command(
        OUTPUT  ${CMAKE_BINARY_DIR}/bios_entry.o
        COMMAND ${SAKURU_NASM} -f elf64
                -o ${CMAKE_BINARY_DIR}/bios_entry.o
                ${CMAKE_SOURCE_DIR}/bios/stage2/entry.asm
        DEPENDS ${CMAKE_SOURCE_DIR}/bios/stage2/entry.asm
        COMMENT "Assembling BIOS stage2 entry")
endif()

add_executable(stage2 ${BIOS_STAGE2_SOURCES})
target_compile_options(stage2 PRIVATE -m64 -fno-pic -mno-red-zone)
target_include_directories(stage2 PRIVATE ${CMAKE_SOURCE_DIR})

target_link_options(stage2 PRIVATE
    -T${CMAKE_SOURCE_DIR}/bios/stage2/linker.ld
    -nostdlib)

# Convert stage2 ELF → flat binary
find_program(OBJCOPY_NATIVE objcopy REQUIRED)
add_custom_command(TARGET stage2 POST_BUILD
    COMMAND ${OBJCOPY_NATIVE} -O binary
            $<TARGET_FILE:stage2>
            ${CMAKE_BINARY_DIR}/stage2.bin
    COMMENT "Converting stage2 ELF → flat binary")
