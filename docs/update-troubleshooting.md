# UART Update Troubleshooting

## Expected Update Flow

The host sends a `.bin` over USART2 at 115200 baud. The application UART receiver writes the image to the staging region at `0x08080000`, checks the image CRC, records the apply request in metadata, and resets. The bootloader checks the staged CRC again, copies the image to the active application region at `0x08008000`, clears the apply request, and starts the application.

The same host command accepts either a new image or a previously saved image for rollback:

```powershell
python tools/updater.py send COM3 115200 path\to\application.bin
```

The image must be linked for `0x08008000` and fit within the 480 KB application region. Keep the application running while sending, or use the bootloader's UART update window after reset.

## Failure and Root Cause

The first transfer attempt on COM3 reached the first data chunk but received no valid response. The host had been changed to prepend a four-byte offset to every chunk. That made each normal chunk 260 bytes, while the existing application parser accepts a maximum payload of 256 bytes. Oversized packets are discarded by that parser, so the host retried and eventually reported the first chunk as failed.

The active application and its staging protocol already use sequential 256-byte chunks. The fix was to restore that wire format in `tools/updater.py`; chunk offsets remain host-side only. Chunks are padded to an 8-byte boundary for STM32 double-word flash programming. The bootloader fallback receiver was aligned to the same packet format.

## Experiments and Build Issues

- Initially, inspection focused on the bootloader, which did not have a UART receiver. A bootloader-side receiver was added as a fallback, including staging erase/write, packet CRC checks, image CRC verification, vector-table checks, and metadata apply handling.
- An offset-addressed chunk format and corresponding application receiver change were tried. This would have required reflashing a compatible application before the Python sender could use it, so that experiment was reverted in favor of the existing application's 256-byte protocol.
- The generated Debug make recipes include `-fcyclomatic-complexity`, which the installed Arm GNU Toolchain 15.3.1 rejects. Removing that analysis-only option from the generated bootloader make recipes allowed the bootloader build to complete. The generated application make recipes still have the same toolchain issue; no application source change was needed for the successful transfer.
- Python syntax and packet framing were checked. The final write payload is 256 bytes, within the application's configured limit.

## Successful Transfer

The corrected command was run with `application/Application/Debug/Application.bin` on COM3. The image was 21,008 bytes with CRC32 `0xACB41778`; all 83 chunks were acknowledged, and `END_UPDATE` returned the CRC/apply acknowledgment. The device then reset to install the staged image. The transfer acknowledgment was observed; post-reset application behavior was not separately captured over the serial console.

The failed transfer occurred before the apply request was set, so it did not ask the bootloader to replace the active application. For future rollbacks, retain the old `.bin` on the host and send it with the same command.