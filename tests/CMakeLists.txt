# ── SakuruBoot unit test suite ─────────────────────────────────────────
cmake_minimum_required(VERSION 3.22)

enable_testing()

set(TEST_COMPILE_OPTIONS
    -std=c11
    -Wall -Wextra -Wno-unused-parameter
    -g -fsanitize=address,undefined
)
set(TEST_COMPILE_DEFS SAKURU_HOST_TEST)
set(TEST_LINK_FLAGS   -fsanitize=address,undefined)
set(SRC ${CMAKE_SOURCE_DIR})

# ── test_config ─────────────────────────────────────────────────────────
add_executable(test_config
    ${SRC}/tests/unit/test_config.c
    ${SRC}/common/config.c)
target_compile_options(test_config     PRIVATE ${TEST_COMPILE_OPTIONS})
target_compile_definitions(test_config PRIVATE ${TEST_COMPILE_DEFS})
target_link_options(test_config        PRIVATE ${TEST_LINK_FLAGS})
target_include_directories(test_config PRIVATE ${SRC} ${SRC}/tests/framework)
add_test(NAME test_config COMMAND test_config)
set_tests_properties(test_config PROPERTIES TIMEOUT 30)

# ── test_crypto ─────────────────────────────────────────────────────────
add_executable(test_crypto
    ${SRC}/tests/unit/test_crypto.c
    ${SRC}/crypto/sha.c
    ${SRC}/crypto/hmac.c
    ${SRC}/crypto/pbkdf2.c
    ${SRC}/crypto/blake2b.c
    ${SRC}/crypto/aes.c)
target_compile_options(test_crypto     PRIVATE ${TEST_COMPILE_OPTIONS})
target_compile_definitions(test_crypto PRIVATE ${TEST_COMPILE_DEFS})
target_link_options(test_crypto        PRIVATE ${TEST_LINK_FLAGS})
target_include_directories(test_crypto PRIVATE ${SRC} ${SRC}/tests/framework)
target_link_libraries(test_crypto m)
add_test(NAME test_crypto COMMAND test_crypto)
set_tests_properties(test_crypto PROPERTIES TIMEOUT 30)

# ── test_luks ───────────────────────────────────────────────────────────
add_executable(test_luks
    ${SRC}/tests/unit/test_luks.c)
target_compile_options(test_luks     PRIVATE ${TEST_COMPILE_OPTIONS})
target_compile_definitions(test_luks PRIVATE ${TEST_COMPILE_DEFS})
target_link_options(test_luks        PRIVATE ${TEST_LINK_FLAGS})
target_include_directories(test_luks PRIVATE ${SRC} ${SRC}/tests/framework)
add_test(NAME test_luks COMMAND test_luks)
set_tests_properties(test_luks PROPERTIES TIMEOUT 30)
