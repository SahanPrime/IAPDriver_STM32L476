#!/usr/bin/env python3
"""
Firmware updater for stm32l476-iap-driver.
Usage:
    python updater.py send <port> <baud> <firmware.bin>   - send, verify, and automatically install firmware
    python updater.py apply <port> <baud>                  - install a previously staged image
"""

import serial
import struct
import sys
import time
import zlib

START_BYTE       = 0xAA
CMD_START_UPDATE = 0x01
CMD_WRITE_CHUNK  = 0x02
CMD_END_UPDATE   = 0x03
CMD_ACK          = 0x04
CMD_NACK         = 0x05
CMD_APPLY_UPDATE = 0x06

CHUNK_SIZE  = 256
ACK_TIMEOUT = 2.0
MAX_RETRIES = 5


def build_packet(cmd: int, payload: bytes) -> bytes:
    length = len(payload)
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    return bytes([START_BYTE, cmd]) + struct.pack(">H", length) + payload + struct.pack(">I", crc)


def read_full_packet(ser: serial.Serial):
    while True:
        start = ser.read(1)
        if len(start) != 1:
            return None
        if start[0] == START_BYTE:
            break

    header = ser.read(3)
    if len(header) != 3:
        return None
    cmd = header[0]
    length = struct.unpack(">H", header[1:3])[0]
    payload = ser.read(length)
    if len(payload) != length:
        return None
    crc_bytes = ser.read(4)
    if len(crc_bytes) != 4:
        return None
    received_crc = struct.unpack(">I", crc_bytes)[0]
    if received_crc != (zlib.crc32(payload) & 0xFFFFFFFF):
        return None
    return (cmd, payload)


def send_packet_and_wait_ack(ser: serial.Serial, cmd: int, payload: bytes) -> bool:
    packet = build_packet(cmd, payload)
    for attempt in range(1, MAX_RETRIES + 1):
        ser.write(packet)
        ser.timeout = ACK_TIMEOUT
        result = read_full_packet(ser)
        if result is not None and result[0] == CMD_ACK:
            return True
        elif result is not None and result[0] == CMD_NACK:
            print(f"  NACK, retrying ({attempt}/{MAX_RETRIES})...")
        else:
            print(f"  No/invalid response, retrying ({attempt}/{MAX_RETRIES})...")
    return False


def pad_to_multiple_of_8(data: bytes) -> bytes:
    remainder = len(data) % 8
    if remainder == 0:
        return data
    return data + (b"\x00" * (8 - remainder))


def send_firmware(port: str, baud: int, bin_path: str):
    with open(bin_path, "rb") as f:
        firmware = f.read()

    real_size = len(firmware)
    final_crc = zlib.crc32(firmware) & 0xFFFFFFFF

    print(f"Firmware: {bin_path}  ({real_size} bytes, CRC32 0x{final_crc:08X})")

    with serial.Serial(port, baud, timeout=ACK_TIMEOUT) as ser:
        time.sleep(0.5)

        start_payload = struct.pack(">II", real_size, final_crc)
        print("Sending START_UPDATE...")
        if not send_packet_and_wait_ack(ser, CMD_START_UPDATE, start_payload):
            print("FAILED: START_UPDATE rejected")
            sys.exit(1)

        offset = 0
        chunk_num = 0
        total_chunks = (real_size + CHUNK_SIZE - 1) // CHUNK_SIZE

        while offset < real_size:
            chunk = pad_to_multiple_of_8(firmware[offset: offset + CHUNK_SIZE])
            chunk_num += 1
            print(f"  Chunk {chunk_num}/{total_chunks}...", end="\r")

            if not send_packet_and_wait_ack(ser, CMD_WRITE_CHUNK, chunk):
                print(f"\nFAILED: chunk {chunk_num} rejected")
                sys.exit(1)

            offset += CHUNK_SIZE

        print(f"\nAll {total_chunks} chunks sent. Sending END_UPDATE...")
        if not send_packet_and_wait_ack(ser, CMD_END_UPDATE, b""):
            print("FAILED: END_UPDATE rejected (CRC mismatch or incomplete)")
            sys.exit(1)

        print("Image CRC verified. Device is rebooting to install the update.")


def send_apply_command(port: str, baud: int):
    with serial.Serial(port, baud, timeout=ACK_TIMEOUT) as ser:
        time.sleep(0.5)
        print("Sending APPLY_UPDATE...")
        if send_packet_and_wait_ack(ser, CMD_APPLY_UPDATE, b""):
            print("Apply confirmed - device is resetting and installing the update.")
        else:
            print("FAILED: device did not acknowledge apply command")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    action = sys.argv[1]

    if action == "send" and len(sys.argv) == 5:
        send_firmware(sys.argv[2], int(sys.argv[3]), sys.argv[4])
    elif action == "apply" and len(sys.argv) == 4:
        send_apply_command(sys.argv[2], int(sys.argv[3]))
    else:
        print(__doc__)
        sys.exit(1)