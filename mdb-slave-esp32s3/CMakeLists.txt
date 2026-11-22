# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# App version = nearest *firmware* tag. Plain `git describe` (ESP-IDF default) also
# matches android-v* tags in this monorepo and would report e.g. "android-v1.2.8".
execute_process(
    COMMAND git describe --tags --always --dirty --match "firmware-v*" --match "v[0-9]*"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE PROJECT_VER
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT PROJECT_VER)
    set(PROJECT_VER "unknown")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mdb-slave-esp32s3)
