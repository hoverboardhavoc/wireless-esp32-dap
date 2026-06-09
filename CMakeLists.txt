cmake_minimum_required(VERSION 3.5)
# Colored  Compiler Output with ninja
add_compile_options (-fdiagnostics-color=always)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# This fork: OTA is unused (USE_OTA=0, we flash over USB), and the corsacOTA
# submodule doesn't build on IDF v5.x (removed ESP_ERR_FLASH_OP_* codes, mbedtls_sha1_ret).
# Exclude it from the build so the result is deterministic across hosts/CI.
set(EXCLUDE_COMPONENTS corsacOTA)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wireless_esp_dap)
