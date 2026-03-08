# ── UEFI AArch64 build target ────────────────────────────────────────────
cmake_minimum_required(VERSION 3.22)

set(LUKS_SOURCES
    ${CMAKE_SOURCE_DIR}/crypto/sha.c
    ${CMAKE_SOURCE_DIR}/crypto/hmac.c
    ${CMAKE_SOURCE_DIR}/crypto/pbkdf2.c
    ${CMAKE_SOURCE_DIR}/crypto/blake2b.c
    ${CMAKE_SOURCE_DIR}/crypto/argon2.c
    ${CMAKE_SOURCE_DIR}/crypto/aes.c
    ${CMAKE_SOURCE_DIR}/crypto/cipher.c
    ${CMAKE_SOURCE_DIR}/luks/luks1.c
    ${CMAKE_SOURCE_DIR}/luks/luks2.c
    ${CMAKE_SOURCE_DIR}/luks/luks_vol.c
    ${CMAKE_SOURCE_DIR}/common/passphrase.c)

set(UEFI_ARM_SOURCES
    ${CMAKE_SOURCE_DIR}/uefi/aarch64/entry.c
    ${CMAKE_SOURCE_DIR}/uefi/uefi_loader.c
    ${CMAKE_SOURCE_DIR}/uefi/ext4.c
    ${CMAKE_SOURCE_DIR}/common/config.c
    ${CMAKE_SOURCE_DIR}/common/mem.c
    ${CMAKE_SOURCE_DIR}/common/menu.c
    ${CMAKE_SOURCE_DIR}/common/efivar.c
    ${CMAKE_SOURCE_DIR}/common/bootcount.c
    ${CMAKE_SOURCE_DIR}/common/loader_entry.c
    ${CMAKE_SOURCE_DIR}/common/serial.c
    ${CMAKE_SOURCE_DIR}/os/elf_loader.c
    ${CMAKE_SOURCE_DIR}/os/vios.c
    ${CMAKE_SOURCE_DIR}/os/linux.c
    ${CMAKE_SOURCE_DIR}/os/windows.c
    ${LUKS_SOURCES})

# Step 1: Build ELF
add_executable(BOOTAA64_elf ${UEFI_ARM_SOURCES})
set_target_properties(BOOTAA64_elf PROPERTIES OUTPUT_NAME "BOOTAA64")
target_include_directories(BOOTAA64_elf PRIVATE ${CMAKE_SOURCE_DIR})

# Step 2: Convert ELF → flat binary (QEMU AARCH64 UEFI)
if(SAKURU_OBJCOPY)
    add_custom_command(TARGET BOOTAA64_elf POST_BUILD
        COMMAND ${SAKURU_OBJCOPY} -O binary
            $<TARGET_FILE:BOOTAA64_elf>
            ${CMAKE_BINARY_DIR}/BOOTAA64.EFI
        COMMENT "Converting BOOTAA64 ELF → flat EFI binary")
endif()
