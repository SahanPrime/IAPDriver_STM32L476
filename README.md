# UART Bootloader for STM32L476RG

This repository contains a UART-based bootloader for the STM32L476RG (NUCLEO-L476RG). It demonstrates manual flash programming, linker script memory layout control, vector-table relocation, and a custom firmware update protocol for in-system application updates.

## What is in this repo

- `bootloader/Bootloader/` — bootloader project. Runs first and manages the app update flow.
- `application/Application/` — application project linked to the application flash region.
- `application/Application_Update/` — alternate application image used to validate update behavior.
- `tools/` — Python utilities for debug and update operations.
- `docs/` — project documentation, memory layout notes, and release notes.

## Why we need a bootloader

A bootloader is needed when a microcontroller must be updated without removing the chip or using a separate external programmer every time. In embedded systems, firmware often needs to be patched, upgraded, or recovered in the field after deployment.

This project solves that problem by letting the STM32 device:

- start in a small trusted bootloader;
- accept a new firmware image over UART;
- write that image into flash at a safe address;
- verify the update;
- jump to the new application image.

Without a bootloader, updating firmware usually means physically disconnecting the board, using a debugger/programmer, and re-flashing the device manually. That is time-consuming, expensive, and impossible in many deployed or remote applications.

A bootloader is also essential for recovery. If a new firmware image is corrupted or a bug is introduced, the bootloader can provide a way to reprogram the device instead of leaving the product bricked.

## Problems this project solves

This repository addresses several real embedded engineering challenges:

- Safe firmware upgrades without removing the MCU from the system.
- Separation of bootloader and application code in flash memory.
- Manual control of memory regions and linker script layout.
- Vector table relocation so the application can run correctly after the bootloader hands control over.
- In-system update flow using a simple UART packet protocol.
- Recovery and validation of alternate application images.
- Clear demonstration of low-level memory, reset, and startup behavior on Cortex-M devices.

In other words, the project shows how a real embedded device can be upgraded in a controlled way, rather than treating firmware as a static image that is only programmed once at manufacturing time.

## Pros of this project

- Excellent learning project for embedded firmware and low-level systems.
- Demonstrates real-world bootloader concepts such as flash programming and startup control.
- Teaches memory layout fundamentals, vector tables, linker scripts, and reset logic.
- Simple UART-based update mechanism that is easy to understand and debug.
- Good foundation for building more advanced bootloaders with checksums, authentication, or OTA updates.
- Works directly on STM32 hardware and shows how firmware can be organized across multiple flash regions.

## Cons of this project

- Not production-grade security or reliability for commercial products.
- Uses a simple UART protocol without encryption, signing, or secure authentication.
- Manual update workflow is less convenient than a full OTA or cloud-based solution.
- No robust error handling for all possible flash or communication faults.
- Limited by UART transport speed and basic validation logic.
- Designed as a teaching and demonstration project, not a full firmware-management ecosystem.

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

Build the bootloader and program `bootloader/Bootloader/Debug/Bootloader.bin` using ST-LINK at address `0x08000000`. The application update receiver listens on USART2 at 115200 baud. It stages and validates incoming firmware before writing it to flash and jumping into the application.

Install the host dependency with `python -m pip install pyserial`, then run:

```powershell
python tools/updater.py send COM3 115200 application/Application/Debug/Application.bin
```

Replace `COM3` with the board's serial port and the final argument with the `.bin` to install. Run the command while the application is running. The same command installs an older `.bin` for rollback testing if necessary.

## Notes

This project is centered on low-level embedded programming and firmware update mechanisms. It does not include a separate CI pipeline, package system, or additional application framework beyond the bootloader/application update flow itself.
