# UART Bootloader for STM32L476RG

This repository contains a UART-based bootloader for the STM32L476RG (NUCLEO-L476RG). It demonstrates manual flash programming, linker script memory layout control, vector-table relocation, and a simple firmware update workflow over UART.

## What is in this repo

- `bootloader/Bootloader/` — bootloader project. Runs first and manages the app update flow.
- `application/Application/` — application project linked to the application flash region.
- `application/Application_Update/` — alternate application image used to validate update behavior.
- `tools/` — Python utilities for debug and update operations.
- `docs/` — project documentation, memory layout notes, and release notes.

## Current status

The bootloader can validate an application image and jump into it from a separate flash region. The project is focused on the embedded bootloader/update workflow and the supporting documentation.

## Hardware

- MCU: STM32L476RG
- Board: Nucleo-L476RG
- Debug/flash: ST-LINK
- Communication: UART (USART2)

## Documentation

- [Memory Map](docs/memory-map.md)
- [Design Notes](docs/design-notes.md)
- [Hardware and Firmware Guide](docs/hardware-and-firmware-guide.md)
- [UART Update Troubleshooting](docs/update-troubleshooting.md)

## Build and flash

This repository is structured as STM32CubeIDE projects. Open the project folders in STM32CubeIDE or use the generated STM32 toolchain configuration to build and flash the firmware.

The bootloader and application are intentionally built for different flash regions so they can coexist on the same MCU:

- Bootloader: `0x08000000`
- Application: `0x08008000`

## UART application update

Build the bootloader and program `bootloader/Bootloader/Debug/Bootloader.bin` using ST-LINK at address `0x08000000`. The application update receiver listens on USART2 at 115200 baud. It stages and CRC-checks the image, then resets; the bootloader copies the verified image into the active application slot and starts it.

Install the host dependency with `python -m pip install pyserial`, then run:

```powershell
python tools/updater.py send COM3 115200 application/Application/Debug/Application.bin
```

Replace `COM3` with the board's serial port and the final argument with the `.bin` to install. Run the command while the application is running. The same command installs an older `.bin` for rollback. The image must be linked for `0x08008000` and be no larger than 480 KB. A rejected or incomplete transfer does not request the bootloader to replace the active image.

## Notes

This project is centered on low-level embedded programming and firmware update mechanisms. It does not include a separate CI pipeline, package system, or additional application framework beyond the project files and helper scripts in this repository.
