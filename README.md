# UART Bootloader — STM32L476RG

A compact, UART-based two-stage bootloader for the STM32L476RG (NUCLEO-L476RG). This project demonstrates low-level embedded systems techniques including manual flash programming, linker-script memory layout, and safe firmware update mechanisms.

## Status

Active development — basic bootloader-to-application jump is implemented (v0.1). UART update protocol and host tooling are in progress. See [`docs/`](docs/) for design notes and detailed specifications.

## Key Concepts

- Two-stage bootloader: a small, robust bootloader that can receive and write firmware images to flash, then hand execution to the application.
- Safe flash operations: flash writes and erase operations are page-aligned and validated to avoid corrupting the bootloader.
- Vector table relocation: application vector table is relocated when control is transferred so interrupts are handled correctly by the running image.
- Simple, extensible UART protocol: framed packets with CRC and command set for erase, write, verify, and jump.

## Repository layout

- bootloader/      — Bootloader firmware (runs first). Handles receiving updates over UART, writing to flash, and jumping to the application.
- application/     — Example application built to run at the application flash offset. Used to validate the bootloader's jump behavior and demonstrate application updates.
- host-tool/       — Host-side Python utility to send firmware images over UART using the custom protocol.
- docs/            — Design notes, memory map, and protocol specification. Reference for development decisions and protocol details.
- tools/           — Build helpers, scripts, or utilities used during development (CI, flashing helpers, etc.).
- examples/        — Example firmware images or test payloads for exercising the bootloader.

(If any of the above folders are not present yet, they reflect intended structure and will be committed as they are added.)

## Memory map & protocol

See [`docs/memory-map.md`](docs/memory-map.md) for the flash and SRAM layout and the rationale for the bootloader/app split. See [`docs/protocol-spec.md`](docs/protocol-spec.md) for the UART packet format and command set (working draft).

## Building & flashing

Toolchain and build instructions will be documented here once the project stabilizes. Typical steps will include installing an ARM GCC toolchain, invoking the provided Makefile/CMake configuration, and using OpenOCD or similar tools to flash the bootloader image.

## Contributing

Contributions, issues, and suggestions are welcome. Please open an issue or submit a pull request with proposed changes.
