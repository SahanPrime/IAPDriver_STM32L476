# Memory Map

## Flash Layout

| Region | Start Address | End Address | Size |
|---|---:|---:|---:|
| Bootloader | 0x08000000 | 0x08007FFF | 32 KB |
| Active application | 0x08008000 | 0x0807FFFF | 480 KB |
| Staging image | 0x08080000 | 0x080F7FFF | 480 KB |
| Metadata area | 0x080F8000 | 0x080FFFFF | 32 KB |

Total flash on the STM32L476RG: 1024 KB (1 MB), starting at `0x08000000`
(the fixed flash base address for this chip, per RM0351). The four regions
above sum to the full 1024 KB. The active/staging sizes and metadata location
are defined in `bootloader/Bootloader/Core/Inc/flash_map.h`.

## Why this split, and why 32K

The bootloader and application are two separate programs that must coexist
permanently in the same physical flash chip, so they need non-overlapping
address ranges.

**Why 32K for the bootloader specifically:**
- Must be a multiple of the flash page size (2 KB on the STM32L4), since
  flash erase operations work on whole pages. 32 KB = 16 pages exactly.
- Large enough to hold the bootloader with room for update handling: the
  current Debug build is about 23.3 KiB of `.text` + `.data` out of 32 KiB.
- Leaves 992 KB after the bootloader for the active image, staging image,
  and metadata. The current update design reserves 32 KB of that space for
  metadata and divides the remaining 960 KB equally between active/staging.

## SRAM

Both the bootloader and application projects use the **full 96 KB** of
SRAM1 (`0x20000000`–`0x20017FFF`), unsplit. Since only one program
executes at a time, there's no need to partition RAM between them the same
way flash is partitioned.

`RAM2` (32 KB at `0x10000000`) exists as a separate physical SRAM
instance on this chip but isn't used by either project.

## Vector Table Relocation (VTOR)

When the bootloader jumps to the application, the CPU's `SCB->VTOR`
register must be updated to point at the application's vector table
(`0x08008000`) instead of the bootloader's (`0x08000000`).

**Implementation:** The bootloader sets `SCB->VTOR` to the active app address
in `go2APP()`, and the application's `system_stm32l4xx.c` also sets
`VECT_TAB_OFFSET = 0x8000` during system initialization. Both ensure
exceptions use the application vector table at `0x08008000`.

## The Jump Sequence (`go2APP()`)

From the bootloader's perspective, jumping to the application is
functionally equivalent to a fresh reset. The bootloader has to manually
replay the setup the CPU normally performs at reset:

1. **Validity check** — read the value at the application's start
   address (`0x08008000`) and confirm it looks like a plausible SRAM
   address before attempting to jump.
2. **Read the application's initial stack pointer** — the first word at
   `0x08008000`.
3. **Set the CPU's main stack pointer** to that value via `__set_MSP()`.
4. **Read the application's reset handler address** — the second word,
   at `0x08008004`.
5. **Jump** — cast that address to a function pointer and call it,
   transferring control into the application's `Reset_Handler`.
