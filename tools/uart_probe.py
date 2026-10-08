"""
UART probe for the STM32L476RG Application (matches uart_protocol.c / .h).

Usage:
    python uart_probe.py listen  COM3   # print whatever the board sends (press RESET)
    python uart_probe.py good    COM3   # valid START_UPDATE   -> expect ACK  (cmd 0x04)
    python uart_probe.py badcrc  COM3   # deliberately bad CRC -> expect NACK (cmd 0x05)

Packet format (firmware side):
    START(0xAA) CMD LEN_HI LEN_LO PAYLOAD CRC32(big-endian, over PAYLOAD only)
"""
import struct
import sys
import time
import zlib

import serial  # pip install pyserial

BAUD = 115200
START_BYTE = 0xAA
CMD_START_UPDATE = 0x01
CMD_ACK = 0x04
CMD_NACK = 0x05
NAMES = {CMD_ACK: "ACK", CMD_NACK: "NACK"}


def hexdump(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data) if data else "(nothing)"


def build_packet(cmd: int, payload: bytes = b"", corrupt_crc: bool = False) -> bytes:
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    if corrupt_crc:
        crc ^= 0xDEADBEEF
    return (bytes([START_BYTE, cmd]) + struct.pack(">H", len(payload))
            + payload + struct.pack(">I", crc))


def start_update_payload(size: int, fw_crc: int) -> bytes:
    # total size (4B, big-endian) + final firmware CRC (4B, big-endian)
    return struct.pack(">II", size, fw_crc)


def decode_reply(data: bytes) -> str:
    if len(data) >= 2 and data[0] == START_BYTE:
        return f"{hexdump(data)}  -> {NAMES.get(data[1], 'CMD 0x%02X' % data[1])}"
    return hexdump(data)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return
    mode, port = sys.argv[1], sys.argv[2]

    with serial.Serial(port, BAUD, timeout=3) as ser:   # long timeout: START_UPDATE erases flash
        ser.reset_input_buffer()

        if mode == "listen":
            print(f"Listening on {port} @ {BAUD}. Press RESET...")
            end = time.time() + 15
            while time.time() < end:
                data = ser.read(64)
                if data:
                    print(data.decode(errors="replace"), end="", flush=True)
            print("\n[done]")

        elif mode in ("good", "badcrc"):
            payload = start_update_payload(1024, 0)      # dummy size/CRC just for the test
            pkt = build_packet(CMD_START_UPDATE, payload, corrupt_crc=(mode == "badcrc"))
            print("sending:", hexdump(pkt))
            ser.write(pkt)
            time.sleep(1.5)                               # let ALL bytes (debug text + reply) arrive
            raw = ser.read(4096)
            print(f"raw ({len(raw)} bytes) as text:\n{raw.decode(errors='replace')}")
            print("raw as hex:", hexdump(raw))
            # try to find an AA 04/05 ... packet anywhere in the noise
            idx = -1
            for i in range(len(raw) - 1):
                if raw[i] == START_BYTE and raw[i + 1] in (CMD_ACK, CMD_NACK):
                    idx = i
                    break
            if idx >= 0:
                print("found packet:", decode_reply(raw[idx:idx + 8]))
            else:
                print("no AA 04/05 packet found in the reply")

        else:
            print(__doc__)


if __name__ == "__main__":
    main()