# Flip clock firmware for ESP32-C6 on ESP-IDF v6.1.
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Built and tested with v6.1; older versions lack APIs it uses, such as esp_console_new_repl_stdio().
if(IDF_VERSION_MAJOR LESS 6 OR (IDF_VERSION_MAJOR EQUAL 6 AND IDF_VERSION_MINOR LESS 1))
    message(FATAL_ERROR "This project needs ESP-IDF v6.1, but IDF_PATH points to "
                        "v${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}. See esp32/README.md.")
endif()
# Build only main and the components it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(flipclock)
