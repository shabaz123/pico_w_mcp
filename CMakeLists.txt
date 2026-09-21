cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pico_mcp_cpp C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_mcp_cpp
    main.cpp
    mcp_server.cpp
)

target_compile_definitions(pico_mcp_cpp PRIVATE
    WIFI_SSID="${WIFI_SSID}"
    WIFI_PASSWORD="${WIFI_PASSWORD}"
)

target_include_directories(pico_mcp_cpp PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(pico_mcp_cpp
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
)

pico_enable_stdio_usb(pico_mcp_cpp 1)
pico_enable_stdio_uart(pico_mcp_cpp 0)

pico_add_extra_outputs(pico_mcp_cpp)
