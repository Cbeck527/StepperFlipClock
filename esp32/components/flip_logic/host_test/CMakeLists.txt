# Builds and runs the flip_logic unit tests on the host, with no board and no ESP-IDF build.
# Run from this directory: cmake --workflow --preset host
cmake_minimum_required(VERSION 3.25)
project(flip_logic_host_test C)

# Unity ships with ESP-IDF, so the tests need IDF_PATH but nothing else from it.
if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH is not set. Activate ESP-IDF first; see esp32/README.md.")
endif()
set(UNITY_DIR "$ENV{IDF_PATH}/components/unity/unity/src")

add_library(unity STATIC "${UNITY_DIR}/unity.c")
target_include_directories(unity PUBLIC "${UNITY_DIR}")

add_executable(test_flip_logic test_flip_logic.c ../flip_logic.c)
target_include_directories(test_flip_logic PRIVATE ../include)
target_link_libraries(test_flip_logic PRIVATE unity)
# Sanitizers turn the kind of undefined behaviour the sketch had into test failures.
target_compile_options(test_flip_logic PRIVATE
    -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all)
target_link_options(test_flip_logic PRIVATE -fsanitize=address,undefined)

enable_testing()
add_test(NAME flip_logic COMMAND test_flip_logic)
