# cmake/SignEFI.cmake — Secure Boot signing helper
#
# Defines sakuru_sign_efi(target output_path) which creates a
# "sign-efi" custom target using sbsign (from sbsigntool) or sbctl.
#
# Required CMake variables (set by caller or command line):
#   SAKURU_SB_KEY   - path to PEM private key  (e.g. MOK.key)
#   SAKURU_SB_CERT  - path to PEM certificate  (e.g. MOK.crt)
#
# Usage in target CMakeLists.txt:
#   include(${CMAKE_SOURCE_DIR}/cmake/SignEFI.cmake)
#   sakuru_sign_efi(BOOTX64 ${CMAKE_BINARY_DIR}/BOOTX64_signed.EFI)

find_program(SBSIGN sbsign DOC "sbsign (sbsigntool) for Secure Boot signing")
find_program(SBCTL  sbctl  DOC "sbctl for Secure Boot key management")

function(sakuru_sign_efi target signed_output)
    if(NOT SBSIGN AND NOT SBCTL)
        message(WARNING "Neither sbsign nor sbctl found — Secure Boot signing disabled. "
                        "Install sbsigntool or sbctl to enable signing.")
        return()
    endif()

    if(SBSIGN)
        if(NOT SAKURU_SB_KEY OR NOT SAKURU_SB_CERT)
            message(WARNING "SAKURU_SB_KEY and SAKURU_SB_CERT must be set for sbsign. "
                            "Run: cmake -DSAKURU_SB_KEY=MOK.key -DSAKURU_SB_CERT=MOK.crt .")
            return()
        endif()

        add_custom_target(sign-efi
            COMMAND ${SBSIGN}
                --key  "${SAKURU_SB_KEY}"
                --cert "${SAKURU_SB_CERT}"
                --output "${signed_output}"
                $<TARGET_FILE:${target}>
            DEPENDS ${target}
            COMMENT "Signing ${target} with sbsign (MOK key)")

    elseif(SBCTL)
        add_custom_target(sign-efi
            COMMAND ${SBCTL} sign --output "${signed_output}"
                $<TARGET_FILE:${target}>
            DEPENDS ${target}
            COMMENT "Signing ${target} with sbctl")
    endif()

    message(STATUS "Secure Boot signing configured → ${signed_output}")
endfunction()
