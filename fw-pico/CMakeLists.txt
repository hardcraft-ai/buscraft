cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w CACHE STRING "Board type")
# The SDK's own import script, used in place from the SDK checkout (PICO_SDK_PATH on the command line or in the environment)
if(NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if(NOT PICO_SDK_PATH)
    message(FATAL_ERROR "Set PICO_SDK_PATH to a Pico SDK checkout")
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(buscraft_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(BUSCRAFT_VERSION "0.1.0" CACHE STRING "Firmware version")
option(BUSCRAFT_SWAP_NRFD_NDAC "NRFD on GP10 and NDAC on GP9, for a board wired with the two crossed" OFF)
option(BUSCRAFT_SWAP_DAV_REN "DAV on GP15 and REN on GP8, for a board wired with the two crossed" OFF)
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
    BUSCRAFT_SWAP_NRFD_NDAC=$<BOOL:${BUSCRAFT_SWAP_NRFD_NDAC}>
    BUSCRAFT_SWAP_DAV_REN=$<BOOL:${BUSCRAFT_SWAP_DAV_REN}>
)
target_compile_options(buscraft-pico PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(buscraft-pico pico_stdlib pico_flash pico_cyw43_arch_lwip_poll pico_lwip_mdns
    pico_btstack_ble pico_btstack_cyw43 pico_mbedtls pico_rand pico_unique_id)
pico_btstack_make_gatt_header(buscraft-pico PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/buscraft.gatt")
pico_enable_stdio_usb(buscraft-pico 1)
pico_enable_stdio_uart(buscraft-pico 0)
pico_add_extra_outputs(buscraft-pico)
