# Memory Map

## Flash Layout

| Region      | Start Address | End Address  | Size  |
|-------------|---------------|--------------|-------|
| Bootloader  | 0x08000000    | 0x08007FFF   | 32 KB |
| Application | 0x08008000    | 0x080FFFFF   | 992 KB |

Total flash on the STM32L476RG: 1024 KB (1 MB), starting at `0x08000000`
(the fixed flash base address for this chip, per RM0351).

## Why this split, and why 32K

The bootloader and application are two separate programs that must coexist
permanently in the same physical flash chip, so they need non-overlapping
address ranges.

**Why 32K for the bootloader specifically:**
- Must be a multiple of the flash page size (2 KB on the STM32L4), since
  flash erase operations work on whole pages. 32 KB = 16 pages exactly.
- Large enough to comfortably hold the bootloader's actual code: current
  build size is ~4960 bytes (`.text` + `.data`) out of the 32K budget.
- Small enough to leave the vast majority of flash (992 KB) available
  for the application itself.

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

**Decision:** VTOR relocation is handled in the application's
`system_stm32l4xx.c`, via `USER_VECT_TAB_ADDRESS` /
`VECT_TAB_OFFSET = 0x8000`, rather than explicitly in the bootloader's
jump function.

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
