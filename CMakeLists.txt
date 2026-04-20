cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(hid_lab C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep last 4KB flash sector free for persistent runtime config.
# - write_config.c targets 0x103FF000, so firmware must only link/use
#   the lower 0x003FF000 bytes of flash.
set(PICO_FLASH_SIZE_BYTES 0x003FF000)

pico_sdk_init()

add_executable(hid_lab
    src/main.c
	src/input_tasks.c
    src/usb_descriptors.c
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
