# ── UEFI x86_64 build target ────────────────────────────────────────────
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

set(UEFI_X86_SOURCES
    ${CMAKE_SOURCE_DIR}/uefi/x86_64/entry.c
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

add_executable(BOOTX64 ${UEFI_X86_SOURCES})
set_target_properties(BOOTX64 PROPERTIES
    OUTPUT_NAME "BOOTX64"
    SUFFIX      ".EFI")

target_include_directories(BOOTX64 PRIVATE ${CMAKE_SOURCE_DIR})

# Copy final EFI to the binary-dir root so CI can find it at build/uefi-x86_64/BOOTX64.EFI
add_custom_command(TARGET BOOTX64 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:BOOTX64>
        ${CMAKE_BINARY_DIR}/BOOTX64.EFI
    COMMENT "Copying BOOTX64.EFI to build root")

# ── Optional Secure Boot signing ────────────────────────────────────────
include(${CMAKE_SOURCE_DIR}/cmake/SignEFI.cmake OPTIONAL)

if(SAKURU_SIGN AND COMMAND sakuru_sign_efi)
    sakuru_sign_efi(BOOTX64 ${CMAKE_BINARY_DIR}/BOOTX64_signed.EFI)
endif()

# ── Install rules ───────────────────────────────────────────────────────
include(GNUInstallDirs)
install(FILES ${CMAKE_BINARY_DIR}/BOOTX64.EFI
        DESTINATION ${CMAKE_INSTALL_DATADIR}/sakuruboot)
