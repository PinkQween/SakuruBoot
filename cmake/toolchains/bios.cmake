# ── BIOS x86 toolchain (native freestanding, 32/64-bit) ─────────────────
set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  x86_64)

# Skip the link phase of CMake's compiler detection (freestanding, no libc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Use native gcc; -m32 for stage1 asm, -m64 for stage2 C
find_program(GCC_NATIVE gcc REQUIRED)
set(CMAKE_C_COMPILER   "${GCC_NATIVE}")
set(CMAKE_ASM_COMPILER "${GCC_NATIVE}")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -no-pie")

set(CMAKE_C_FLAGS_INIT
    "-std=c11 -m64 -ffreestanding -fno-stack-protector -fno-pic \
     -mno-red-zone -O2 -Wall -Wextra -I.")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# NASM is required for stage1 (MBR assembly)
find_program(NASM nasm REQUIRED DOC "NASM assembler for BIOS MBR stage1")
set(SAKURU_NASM "${NASM}" CACHE FILEPATH "NASM assembler")
