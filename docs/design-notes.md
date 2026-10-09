# Design Notes

Running log of design decisions and tradeoffs as the project develops.

## Project overview
This repository implements a UART-based firmware update mechanism for the STM32L476RG (NUCLEO-L476RG). The core idea is:

- the bootloader runs first and stays resident in the lower flash region
- the application runs from a separate flash region at a higher address
- a staged update image can be written into a dedicated flash region
- the bootloader validates the candidate image, copies it into the active app region, and then jumps into it

The repository is organized as follows:
- `bootloader/Bootloader/` — bootloader firmware
- `application/Application/` — active application image
- `application/Application_Update/` — alternate application image used for update testing
- `tools/` — Python utilities for UART debug and update operations
- `docs/` — project documentation, memory layout notes, and release notes

The project is focused on low-level embedded fundamentals:
- manual flash erase/programming
- linker script controlled memory layout
- vector table relocation
- custom UART packet protocol
- CRC verification of firmware images

## Week 1 — Memory map & linker script

### MCU and flash map
The STM32L476RG has 1 MB of flash mapped at the fixed base address:

- Flash base: `0x08000000`
- Total flash size: `1 MB` (`1024 KB`)

The project splits flash into distinct regions:

| Region | Start Address | End Address | Size |
|--------|---------------|-------------|------|
| Bootloader | `0x08000000` | `0x08007FFF` | 32 KB |
| Active application | `0x08008000` | `0x0807FFFF` | 480 KB |
| Staging region | `0x08080000` | `0x080F7FFF` | 480 KB |
| Metadata | `0x080F8000` | `0x080FFFFF` | 32 KB |

This is reflected in the linker files:
- bootloader linker script: `FLASH ORIGIN = 0x08000000`, `LENGTH = 32K`
- application linker script: `FLASH ORIGIN = 0x08008000`, `LENGTH = 480K`

The repository also reserves a metadata region near the end of flash:

- `METADATA_ADDR = 0x080F8000`
- `METADATA_SIZE = 32 KB`

That metadata region stores update state such as:
- whether a staged image is valid
- staging image size
- staging CRC
- whether an apply operation was requested
- application version

### Why split flash this way
The bootloader and application are two separate programs that must coexist permanently in the same physical flash device. That requires non-overlapping flash regions. This differs from RAM, where only one program executes at a time and therefore does not need to be partitioned in the same way.

### Why 32 KB for the bootloader
The bootloader area is 32 KB because:
- it is a multiple of the STM32L4 flash page size
- flash erase operations happen on whole pages
- the flash page size on STM32L4 is 2 KB, so 32 KB = 16 pages exactly
- it leaves ample room for the bootloader code while preserving most of the flash for the application

This matches the documentation in `docs/memory-map.md`.

### SRAM layout
The repository uses the chip’s full 96 KB SRAM1 region:

- `SRAM1`: `0x20000000` to `0x20017FFF`
- no separate RAM split between bootloader and application is used

This is intentional because only one firmware image executes at a time. When the bootloader has completed its work, it transfers control to the active application; they do not run concurrently.

### Vector table relocation
The project documents and implements a jump from bootloader to application by updating the CPU’s vector table location:

- bootloader vector table starts at `0x08000000`
- application vector table starts at `0x08008000`

Before jumping, the bootloader validates the application’s initial stack pointer word and reads the reset handler address from the app vector table. It then sets the main stack pointer and jumps into the application reset handler.

The repo also records the tradeoff that:
- the application relocates `SCB->VTOR` in `system_stm32l4xx.c`
- this ensures interrupts use the application vector table after the jump
- there is a brief window where the bootloader vector table is still active, but interrupts are disabled or not used early in startup

### Jump mechanism
The bootloader’s `go2APP()` routine validates the app location by checking the first word at `0x08008000` to ensure it looks like a valid stack pointer (a sanity check against the `0x20000000` SRAM base).

If valid, it:
1. reads the MSP value from the app vector table
2. sets `__set_MSP(...)`
3. reads the reset handler address from `0x08008004`
4. calls it via a function pointer

This is the standard Cortex-M handoff used when a bootloader jumps to another firmware image in a different flash region.

## Week 2 — Flash driver

### Flash update model
The actual firmware update implementation is based on a staged-image pattern:
- active app region: current firmware that is booted
- staging region: candidate image received from host and validated before activation
- metadata region: state and integrity information

The key files involved are:
- `bootloader/Bootloader/Core/Inc/flash_map.h`
- `bootloader/Bootloader/Core/Inc/metadata.h`
- `bootloader/Bootloader/Core/Src/metadata.c`
- `bootloader/Bootloader/Core/Inc/iap_apply.h`
- `bootloader/Bootloader/Core/Src/iap_apply.c`

### Metadata structure
The metadata block stores:

```c
typedef struct{
    uint32_t magic;
    uint32_t staging_valid;
    uint32_t staging_size;
    uint32_t staging_crc;
    uint32_t apply_requested;
    uint32_t app_version;
} boot_metadata_t;
```

The repo initializes metadata on first boot if the magic value is not present:
- `METADATA_MAGIC = 0xDEADBEEFUL`

This prevents erased flash from being mistaken for a valid update record.

### Flash programming rules
The repo follows STM32 flash requirements:
- flash must be erased before it is programmed
- operations happen at page granularity
- writes are performed using double-word programming (`FLASH_TYPEPROGRAM_DOUBLEWORD`)
- error flags are cleared before erase/write sequences

The update logic does the following:
- validate the candidate image CRC
- erase the active app region if update is requested
- copy the staged bytes into the active region
- clear metadata flags after successful apply

The bootloader does not trust the staged image blindly; it recomputes the CRC directly from flash before approving an apply operation.

### Apply logic
`iap_check_and_apply_update()` performs:
1. read metadata
2. return immediately if `apply_requested == 0`
3. recompute CRC of the staging area and compare with stored metadata
4. if invalid, discard and clear flags
5. erase the active region
6. copy the staging region into the active region
7. clear metadata after success

This is a safe pattern because it validates the actual bytes in flash before applying them.

## Week 3 — UART protocol & framing

### Protocol intent
The repository includes a draft protocol specification (`docs/protocol-spec.md`) and a host-side updater script (`tools/updater.py`) that show the intended UART protocol.

The protocol design in the repo is:
- start byte: `0xAA`
- command byte
- 16-bit payload length
- payload
- 32-bit CRC

This is visible in the Python host tool:

```python
def build_packet(cmd: int, payload: bytes) -> bytes:
    length = len(payload)
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    return bytes([START_BYTE, cmd]) + struct.pack(">H", length) + payload + struct.pack(">I", crc)
```

### Command set
The updater script defines:

- `CMD_START_UPDATE = 0x01`
- `CMD_WRITE_CHUNK = 0x02`
- `CMD_END_UPDATE = 0x03`
- `CMD_ACK = 0x04`
- `CMD_NACK = 0x05`
- `CMD_APPLY_UPDATE = 0x06`

The intended flow is:
1. start update
2. send image in chunks
3. validate end-of-image CRC
4. apply update to active region
5. reset or jump to the installed firmware

### CRC strategy
The project uses CRC32 with zlib-compatible behavior:
- the STM32 hardware CRC peripheral is used
- `HAL_CRC_Calculate(...)` is used
- final XOR (`^ 0xFFFFFFFF`) matches zlib’s `crc32()` behavior

This matches the host-side Python tool, which uses Python's `zlib.crc32()`.

### Data chunking
The Python host tool pads each chunk to 8-byte alignment before sending:
- `CHUNK_SIZE = 256`
- each chunk is padded to a multiple of 8 bytes before flash programming

This matters because the bootloader writes flash using double-word operations.

### UART configuration
The bootloader is configured to use:
- USART2
- baud rate: `115200`
- 8-bit, no parity, 1 stop bit
- TX/RX mode

The debug scripts also assume a Windows COM port:
- `PORT = "COM3"`

The bootloader’s `_write()` function redirects stdout to UART via `HAL_UART_Transmit`, which is how serial debug output is emitted during testing.

## Week 4 — Host tool & polish

### Host updater script
The repository includes a Python updater:

- `tools/updater.py`

This tool supports:
- `send`: transmit a firmware binary to the target over UART
- `apply`: trigger an apply operation after a valid staged image is present

The host-side flow is:
- read firmware file
- compute CRC32
- send `START_UPDATE` with size and CRC
- send chunks sequentially
- send `END_UPDATE`
- wait for ACK/NACK
- optionally send `APPLY_UPDATE`

### Debug utilities
Additional debug scripts exist:
- `tools/debug.py`
- `tools/debug1.py`

These are small serial probes used to send manually crafted packets and inspect raw UART responses. They are useful for:
- confirming bootloader startup
- verifying protocol framing
- debugging packet reception and responses

### Practical lessons captured in the repo
The repository records several real implementation issues and fixes:
- vector table relocation is required when the bootloader jumps to the app
- UART output must be initialized for debug messages
- flash operations must be page-aware and erase-before-write
- metadata state must be recorded reliably to prevent repeated invalid updates
- CRC validation is essential before applying a staged image

## Notable implemented behaviors from the actual code

### Bootloader startup behavior
In `main.c`, the bootloader:
- initializes HAL
- configures the CPU clock
- initializes USART2
- calls `metadata_init_if_needed()`
- calls `iap_check_and_apply_update()`
- then repeatedly calls `go2APP()`

This means the bootloader always checks for an update request before attempting to run the application.

### Jump to application
The `go2APP()` function:
- checks for a plausible stack pointer at `0x08008000`
- disables IRQs and SysTick
- clears NVIC enable/pending states
- sets `SCB->VTOR = FLASH_APP_ADDR`
- sets the MSP
- calls the application reset handler

This is the essential bootloader-to-application transfer mechanism.

### Update application staging logic
The code shows a staged-update architecture that is more than just “flash a new binary directly.” It explicitly separates:
- active app region
- staging region
- metadata state

This is a meaningful design choice for robust firmware updates, especially when the system must avoid corrupting the current app during a failed or partial update.

## Repo-level conclusions
This repository is a deliberate demonstration of firmware update fundamentals, not just a simple UART demo. It is built to teach and practice:

- memory partitioning on embedded flash
- linker script based relocation
- flash erase/write mechanics
- vector-table control on Cortex-M
- safe staging and apply logic
- hardware CRC and packet validation
- UART bootloader protocol design
- host-side updater automation

The repo effectively demonstrates how a bootloader can safely manage firmware updates on an STM32L476 without requiring an RTOS or higher-level framework.

## Summary of current state
The repository is at a working bootloader/application handoff milestone with a documented memory map and a staged-update architecture. The project is organized around:

- bootloader region: `0x08000000` to `0x08007FFF`
- application region: `0x08008000` onward
- staging region: `0x08080000` onward
- metadata region: near `0x080F8000`
- UART update protocol with CRC and command framing
- host-side Python utilities for sending and applying updates

This is the core design logic present in the repository today.
