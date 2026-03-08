# ── UEFI x86_64 toolchain (MinGW-w64 → PE32+) ──────────────────────────
set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  x86_64)

# Skip the link phase of CMake's compiler detection (no libc/entry point)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Prefer x86_64-w64-mingw32 from $PATH
find_program(GCC_CROSS x86_64-w64-mingw32-gcc REQUIRED
    DOC "MinGW-w64 GCC for UEFI x86_64 cross-compilation")

set(CMAKE_C_COMPILER   "${GCC_CROSS}")
set(CMAKE_ASM_COMPILER "${GCC_CROSS}")

# PE32+ subsystem 10 = EFI Application
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-Wl,--subsystem,10 -Wl,-e,efi_main -Wl,--strip-all -nostdlib")

set(CMAKE_C_FLAGS_INIT
    "-std=c11 -ffreestanding -fno-stack-protector -fshort-wchar \
     -mno-red-zone -O2 -Wall -Wextra -DSAKURU_UEFI -DSAKURU_X86_64 -I.")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
