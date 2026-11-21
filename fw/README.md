# Firmware

Adapter firmware for the STM32WBA55 (Bluetooth LE 5.4): GPIB handshake, a `++` command interpreter and a BLE GATT
service (Command / Response / Event / Status), plus a line-based JSON message layer for host tools.

Development board: NUCLEO-WBA55CG.

## Build

Needs `arm-none-eabi-gcc` in `PATH` (or STM32CubeIDE installed on macOS) and git.

```sh
cd fw
make cube     # once: fetches the required parts of ST's STM32CubeWBA into ../lib/
make          # -> build/buscraft.bin
make flash    # ST-LINK; STLINK=<serial> picks a probe when several are connected
```

Prebuilt images are attached to every release.

## Licences

Our own code (`App/`, `STM32_WPAN/App/gpib_svc.c` and the changes to the example) is licensed under Apache-2.0 (see the
repository root). The project skeleton under `Core/`, `STM32_WPAN/` and `System/` is derived from ST's BLE_p2pServer
example; those files keep STMicroelectronics' copyright headers and are licensed under the terms in the
[STM32CubeWBA](https://github.com/STMicroelectronics/STM32CubeWBA) package. The BLE stack and link-layer libraries are
fetched from ST at build time and are not part of this repository.

Note: some comments in the code are still in Finnish. They will be translated as the code is reworked.
