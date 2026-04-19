cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(usb_hid_input_simulation_lab C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(usb_hid_input_simulation_lab
    main.c
    usb_descriptors.c
)

target_include_directories(usb_hid_input_simulation_lab PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(usb_hid_input_simulation_lab
    pico_stdlib
    pico_rand
    hardware_flash
    tinyusb_device
    tinyusb_board
)

pico_add_extra_outputs(usb_hid_input_simulation_lab)
