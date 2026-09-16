# UART Bootloader — STM32L476RG

A compact, UART-based two-stage bootloader for the STM32L476RG (NUCLEO-L476RG). This project demonstrates low-level embedded systems techniques including manual flash programming, linker-script memory management, vector table relocation, and a custom UART packet protocol.

## Status

**v0.1 released** — bootloader-to-application jump is working and verified on hardware. UART firmware-update protocol and host tooling are in progress (v0.2). See [`docs/v0.1-release-notes.md`](docs/v0.1-release-notes.md) for this milestone's details, and [`docs/`](docs/) for design specs.

## Key Concepts

- Two-stage bootloader: a small, robust bootloader that can receive and write firmware images to flash, then hand execution to the application.
- Safe flash operations: flash writes and erase operations are page-aligned and validated to avoid corrupting the bootloader.
- Vector table relocation: application vector table is relocated when control is transferred so interrupts are handled correctly by the running image.
- Simple, extensible UART protocol: framed packets with CRC and command set for erase, write, verify, and jump.

## Repository Layout

- `bootloader/`     — Bootloader firmware (runs first). Handles receiving updates over UART, writing to flash, and jumping to the application.
- `application/`    — Example application built to run at the application flash offset. Used to validate the bootloader's jump behavior and demonstrate application updates.
- `host-tool/`      — Host-side Python utility to send firmware images over UART using the custom protocol.
- `docs/`           — Design notes, memory map, protocol specification, and release notes.
- `tools/`          — Build helpers, scripts, or utilities used during development (CI, flashing helpers, etc.).
- `examples/`       — Example firmware images or test payloads for exercising the bootloader.

(If any of the above folders are not yet present, they reflect intended structure and will be committed as they are added.)

## Documentation

- **[v0.1 Release Notes](docs/v0.1-release-notes.md)** — Overview of the bootloader-to-application jump, hardware setup, and debugging notes from this milestone.
- **[Memory Map](docs/memory-map.md)** — Flash and SRAM layout, rationale for the bootloader/app split, and vector table relocation strategy.
- **[Protocol Spec](docs/protocol-spec.md)** — UART packet format, command set, and error handling (in progress for v0.2).

## Building & Flashing

Toolchain and build instructions will be documented here once the project stabilizes. Typical steps will include installing an ARM GCC toolchain, invoking the provided Makefile/CMake configuration, and using ST-Link for flashing and debugging.

## Contributing

Contributions, issues, and suggestions are welcome. Please open an issue or submit a pull request with proposed changes.
