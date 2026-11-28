# BusCraft

An open wireless GPIB adapter. BusCraft plugs into an instrument's IEEE-488 connector, runs on its own battery and
brings the instrument to your computer over Bluetooth LE, with no cable and no shared ground.

Part of [Hardcraft](https://hardcraft.ai): open bench tools for embedded developers, designed with AI.

> **Status:** in development. First release planned for early December 2026.

## What you get in a release

- **Hardware:** KiCad project plus ready-to-order fabrication files (Gerbers, drill, BOM, placement).
- **Firmware:** prebuilt image and source.
- **Client:** prebuilt `buscraft` binaries for Windows, macOS and Linux. The client exposes each adapter as a raw
  TCP socket (port 5025), so existing VISA tools can talk to the instrument.
- **AI skill:** a ready-made skill so your AI assistant can drive the instrument for you.

## Repository layout

| Path | Contents | Licence |
|---|---|---|
| `hw/` | KiCad project and fabrication outputs | CERN-OHL-S-2.0 |
| `client/` | Host library and command line client (Rust): USB, network, Bluetooth LE | Apache-2.0 |
| `app/` | Desktop app (Tauri) | Apache-2.0 |
| `fw-pico/` | Firmware for the Raspberry Pi Pico W prototype | Apache-2.0 |
| `skill/` | AI assistant skill | Apache-2.0 |
| `docs/` | Documentation and measurement data | CC BY 4.0 |

## Building the client

```sh
cd client
cargo build --release
```

## Licences

Code is licensed under the [Apache License 2.0](LICENSE). Hardware design files are licensed under
[CERN-OHL-S-2.0](hw/LICENSE). Documentation and data are licensed under [CC BY 4.0](docs/LICENSE).

The repository contains original work only: no manuals, datasheets, vendor code or other copyrighted material, not even
in part. See [CONTRIBUTING.md](CONTRIBUTING.md).
