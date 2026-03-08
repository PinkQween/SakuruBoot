# ── SakuruBoot unit test suite ─────────────────────────────────────────
cmake_minimum_required(VERSION 3.22)

enable_testing()

set(TEST_COMPILE_FLAGS
    -std=c11
    -DSAKURU_HOST_TEST
    -I${CMAKE_SOURCE_DIR}
    -I${CMAKE_SOURCE_DIR}/tests/mocks
    -Wall -Wextra -Wno-unused-parameter
    -g -fsanitize=address,undefined
)

set(TEST_LINK_FLAGS -fsanitize=address,undefined)

# ── Helper macro ───────────────────────────────────────────────────────
macro(sakuru_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_compile_options(${target_name} PRIVATE ${TEST_COMPILE_FLAGS})
    target_link_options(${target_name}    PRIVATE ${TEST_LINK_FLAGS})
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/tests/mocks
        ${CMAKE_SOURCE_DIR}/tests/framework)
    add_test(NAME ${target_name} COMMAND ${target_name})
    set_tests_properties(${target_name} PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "ASAN_OPTIONS=abort_on_error=1")
endmacro()

# ── Test executables ───────────────────────────────────────────────────
sakuru_test(test_config ${CMAKE_SOURCE_DIR}/tests/unit/test_config.c)
sakuru_test(test_crypto ${CMAKE_SOURCE_DIR}/tests/unit/test_crypto.c)
sakuru_test(test_luks   ${CMAKE_SOURCE_DIR}/tests/unit/test_luks.c)

# test_crypto needs math library on some platforms
target_link_libraries(test_crypto m)
