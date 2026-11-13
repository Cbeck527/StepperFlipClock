# Builds and runs the unit tests for the plain C parts of the firmware on the host,
# with no board and no ESP-IDF build. Run from this directory: cmake --workflow --preset host
cmake_minimum_required(VERSION 3.25)
project(flipclock_host_test C)

# Unity ships with ESP-IDF, so the tests need IDF_PATH but nothing else from it.
if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH is not set. Activate ESP-IDF first; see esp32/README.md.")
endif()
set(UNITY_DIR "$ENV{IDF_PATH}/components/unity/unity/src")
set(COMPONENTS "${CMAKE_CURRENT_SOURCE_DIR}/../components")

add_library(unity STATIC "${UNITY_DIR}/unity.c")
target_include_directories(unity PUBLIC "${UNITY_DIR}")

find_package(Threads REQUIRED)
enable_testing()

# Sanitizers turn the kind of undefined behaviour the sketch had into test failures.
set(CHECKS -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all)

add_executable(test_flip_logic test_flip_logic.c "${COMPONENTS}/flip_logic/flip_logic.c")
target_include_directories(test_flip_logic PRIVATE "${COMPONENTS}/flip_logic/include")
add_test(NAME flip_logic COMMAND test_flip_logic)

add_executable(test_stepper_core test_stepper_core.c "${COMPONENTS}/stepper/stepper_core.c")
target_include_directories(test_stepper_core PRIVATE "${COMPONENTS}/stepper/include")
target_link_libraries(test_stepper_core PRIVATE Threads::Threads)
add_test(NAME stepper_core COMMAND test_stepper_core)

add_executable(test_ds3231_regs test_ds3231_regs.c "${COMPONENTS}/ds3231/ds3231_regs.c")
target_include_directories(test_ds3231_regs PRIVATE "${COMPONENTS}/ds3231/include")
add_test(NAME ds3231_regs COMMAND test_ds3231_regs)

foreach(test test_flip_logic test_stepper_core test_ds3231_regs)
    target_link_libraries(${test} PRIVATE unity)
    target_compile_options(${test} PRIVATE ${CHECKS})
    target_link_options(${test} PRIVATE -fsanitize=address,undefined)
endforeach()
