cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)

project(buscraft_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(BUSCRAFT_VERSION "0.1.0" CACHE STRING "Firmware version")
option(BUSCRAFT_WIFI_POWERSAVE "Wi-Fi power saving (battery builds); off = lowest latency" OFF)

add_executable(buscraft-pico
    src/main.c
    src/gpib.c
    src/komento.c
    src/net.c
    src/ble.c
)
target_include_directories(buscraft-pico PRIVATE src)
target_compile_definitions(buscraft-pico PRIVATE
    BUSCRAFT_VERSION="${BUSCRAFT_VERSION}"
    USBD_MANUFACTURER="Hardcraft"
    USBD_PRODUCT="BusCraft GPIB"
    PICO_STDIO_DEFAULT_CRLF=0
    CYW43_HOST_NAME="buscraft"
    BUSCRAFT_WIFI_POWERSAVE=$<BOOL:${BUSCRAFT_WIFI_POWERSAVE}>
)
target_compile_options(buscraft-pico PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(buscraft-pico pico_stdlib pico_flash pico_cyw43_arch_lwip_poll pico_lwip_mdns
    pico_btstack_ble pico_btstack_cyw43)
pico_btstack_make_gatt_header(buscraft-pico PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/buscraft.gatt")
pico_enable_stdio_usb(buscraft-pico 1)
pico_enable_stdio_uart(buscraft-pico 0)
pico_add_extra_outputs(buscraft-pico)
