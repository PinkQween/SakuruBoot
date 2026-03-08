# ── UEFI AArch64 toolchain (aarch64-linux-gnu → flat EFI) ───────────────
set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  aarch64)

# Skip the link phase of CMake's compiler detection (no libc/entry point)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Ubuntu ships gcc-aarch64-linux-gnu; bare-metal aarch64-elf-gcc is a fallback
find_program(GCC_CROSS
    NAMES aarch64-linux-gnu-gcc aarch64-elf-gcc
    REQUIRED
    DOC "AArch64 cross GCC for UEFI AArch64 compilation")
find_program(OBJCOPY_CROSS
    NAMES aarch64-linux-gnu-objcopy aarch64-elf-objcopy
    REQUIRED
    DOC "AArch64 objcopy for EFI binary conversion")

set(CMAKE_C_COMPILER   "${GCC_CROSS}")
set(CMAKE_ASM_COMPILER "${GCC_CROSS}")

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-Wl,-e,efi_main -Wl,-Ttext=0 -nostdlib -no-pie")

set(CMAKE_C_FLAGS_INIT
    "-std=c11 -ffreestanding -fno-stack-protector -fshort-wchar \
     -O2 -Wall -Wextra -DSAKURU_UEFI -DSAKURU_AARCH64 -I.")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Remember objcopy for the post-build ELF→flat step
set(SAKURU_OBJCOPY "${OBJCOPY_CROSS}" CACHE FILEPATH "AArch64 objcopy")
