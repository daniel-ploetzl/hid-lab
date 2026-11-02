cmake_minimum_required(VERSION 3.13)

# Must be set before the SDK import, which selects the board. Without it the
# SDK silently falls back to the RP2040 Pico, which this project cannot target.
set(PICO_BOARD pico2 CACHE STRING "Target board; flash layout assumes Pico 2")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(hid_lab C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep last 4KB flash sector free for persistent runtime config.
# - write_config.c targets 0x103FF000, so firmware must only link/use
#   the lower 0x003FF000 bytes of flash.
# - Linker only: never define it in C as PICO_FLASH_SIZE_BYTES; the SDK needs the real size.
set(PICO_FLASH_SIZE_BYTES 0x003FF000)

pico_sdk_init()

if(NOT PICO_PLATFORM MATCHES "^rp2350")
    message(FATAL_ERROR "unsupported platform ${PICO_PLATFORM}: write_config emits RP2350 UF2 files")
endif()

add_executable(hid_lab
    src/main.c
	src/input_tasks.c
    src/usb_descriptors.c
)

target_compile_options(hid_lab PRIVATE
    -Wall
    -Wextra
    -Werror
)

# Exposes the linker region size to the build-time layout checks in src/main.c.
target_compile_definitions(hid_lab PRIVATE
    HID_LAB_LINK_FLASH_BYTES=${PICO_FLASH_SIZE_BYTES}
)

target_include_directories(hid_lab PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries(hid_lab
    pico_stdlib
    pico_rand
    hardware_flash
    tinyusb_device
    tinyusb_board
)

pico_add_extra_outputs(hid_lab)
