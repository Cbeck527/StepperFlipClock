# Flip clock firmware for ESP32-C6 on ESP-IDF v6.1.
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only main and the components it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(flipclock)
