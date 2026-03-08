# cmake/modules/FindNASM.cmake — locate NASM assembler
#
# Provides:
#   NASM_FOUND          — TRUE if NASM was found
#   NASM_EXECUTABLE     — full path to nasm binary
#   NASM_VERSION_STRING — version string from nasm -v

find_program(NASM_EXECUTABLE
    NAMES nasm
    DOC   "Netwide Assembler (NASM)")

if(NASM_EXECUTABLE)
    execute_process(
        COMMAND "${NASM_EXECUTABLE}" -v
        OUTPUT_VARIABLE _nasm_ver_raw
        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.?[0-9]*" NASM_VERSION_STRING
           "${_nasm_ver_raw}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NASM
    REQUIRED_VARS NASM_EXECUTABLE
    VERSION_VAR   NASM_VERSION_STRING)

mark_as_advanced(NASM_EXECUTABLE)
