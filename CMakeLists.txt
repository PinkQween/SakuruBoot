# ============================================================
# SakuruBoot — Root CMakeLists.txt
# ============================================================
# Usage:
#   cmake -B build/host   -DSAKURU_TARGET=host   # unit tests only
#   cmake -B build/uefi-x86_64  -DSAKURU_TARGET=uefi-x86_64  \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/uefi-x86_64.cmake
#   cmake -B build/uefi-aarch64 -DSAKURU_TARGET=uefi-aarch64 \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/uefi-aarch64.cmake
#   cmake -B build/bios -DSAKURU_TARGET=bios \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/bios.cmake
#
# Then: cmake --build <build-dir>
#       ctest --test-dir build/host -V   (unit tests, host target only)
# ============================================================
cmake_minimum_required(VERSION 3.22)

project(SakuruBoot
    VERSION 0.2.0
    DESCRIPTION "Lightweight UEFI & BIOS bootloader with LUKS encryption"
    LANGUAGES C ASM)

# ── Set default target ─────────────────────────────────────
if(NOT SAKURU_TARGET)
    set(SAKURU_TARGET "host")
endif()

message(STATUS "SakuruBoot target: ${SAKURU_TARGET}")

# ── Module path ────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

# ── Version header ─────────────────────────────────────────
configure_file(cmake/version.h.in
               ${CMAKE_BINARY_DIR}/generated/sakuru_version.h @ONLY)
include_directories(${CMAKE_BINARY_DIR}/generated)

# ── Common compile options ─────────────────────────────────
set(SAKURU_COMMON_CFLAGS
    -std=c11
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter)

# ── Subdirectories ─────────────────────────────────────────
if(SAKURU_TARGET STREQUAL "host")
    enable_testing()
    add_subdirectory(tests)

elseif(SAKURU_TARGET STREQUAL "uefi-x86_64")
    add_subdirectory(cmake/targets/uefi-x86_64)

elseif(SAKURU_TARGET STREQUAL "uefi-aarch64")
    add_subdirectory(cmake/targets/uefi-aarch64)

elseif(SAKURU_TARGET STREQUAL "bios")
    add_subdirectory(cmake/targets/bios)

else()
    message(FATAL_ERROR "Unknown SAKURU_TARGET: ${SAKURU_TARGET}. "
                        "Valid: host | uefi-x86_64 | uefi-aarch64 | bios")
endif()
