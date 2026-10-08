import serial
import struct
import time
import zlib
import argparse
import os

# ============================================================
# Protocol
# ============================================================

START = 0x01
DATA  = 0x02
END   = 0x03

ACK   = 0x06
NACK  = 0x15

# Packet size sent to STM32
CHUNK_SIZE = 256

# Number of retransmission attempts
MAX_RETRIES = 5

# Timeout waiting for STM32 response
ACK_TIMEOUT = 2.0


# ============================================================
# Utility functions
# ============================================================

def crc32(data):
    """
    CRC32 compatible with Python zlib.crc32().
    Returns unsigned 32-bit value.
    """
    return zlib.crc32(data) & 0xFFFFFFFF


def wait_for_response(ser):
    """
    Wait for one byte from STM32.
    """
    response = ser.read(1)

    if len(response) != 1:
        return None

    return response[0]


def send_packet_wait_ack(ser, packet, packet_number):
    """
    Send one packet and wait for ACK/NACK.
    Retransmit if necessary.
    """

    for attempt in range(1, MAX_RETRIES + 1):

        ser.write(packet)
        ser.flush()

        response = wait_for_response(ser)

        if response == ACK:
            return True

        if response == NACK:
            print(
                f"Packet {packet_number}: "
                f"NACK (attempt {attempt}/{MAX_RETRIES})"
            )
            continue

        print(
            f"Packet {packet_number}: "
            f"timeout/invalid response "
            f"(attempt {attempt}/{MAX_RETRIES})"
        )

    return False


# ============================================================
# Firmware update
# ============================================================

def send_firmware(port, baudrate, filename):

    # --------------------------------------------------------
    # Read firmware
    # --------------------------------------------------------

    if not os.path.isfile(filename):
        print(f"ERROR: File not found: {filename}")
        return False

    with open(filename, "rb") as f:
        firmware = f.read()

    firmware_size = len(firmware)
    firmware_crc = crc32(firmware)

    print()
    print("========================================")
    print(" STM32 Firmware Update")
    print("========================================")
    print(f"File       : {filename}")
    print(f"Size       : {firmware_size} bytes")
    print(f"CRC32      : 0x{firmware_crc:08X}")
    print(f"Packet size: {CHUNK_SIZE} bytes")
    print("========================================")
    print()

    # Your staging area is 480 KB
    MAX_FIRMWARE_SIZE = 480 * 1024

    if firmware_size == 0:
        print("ERROR: Firmware file is empty.")
        return False

    if firmware_size > MAX_FIRMWARE_SIZE:
        print(
            f"ERROR: Firmware is too large. "
            f"Maximum = {MAX_FIRMWARE_SIZE} bytes"
        )
        return False

    # --------------------------------------------------------
    # Open UART
    # --------------------------------------------------------

    try:
        ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=ACK_TIMEOUT
        )
    except serial.SerialException as e:
        print(f"ERROR opening UART: {e}")
        return False

    try:

        # Give STM32 time to settle
        time.sleep(0.2)

        # ----------------------------------------------------
        # START packet
        #
        # Format:
        #
        #   1 byte  START
        #   4 bytes firmware size
        #   4 bytes firmware CRC
        #
        # Little endian
        # ----------------------------------------------------

        start_packet = struct.pack(
            "<BII",
            START,
            firmware_size,
            firmware_crc
        )

        print("Sending START packet...")

        if not send_packet_wait_ack(ser, start_packet, "START"):
            print("ERROR: STM32 did not ACK START.")
            return False

        print("START ACK received.")
        print()

        # ----------------------------------------------------
        # DATA packets
        #
        # Format:
        #
        #   1 byte  DATA
        #   4 bytes packet number
        #   2 bytes data length
        #   N bytes data
        #
        # ----------------------------------------------------

        total_packets = (
            firmware_size + CHUNK_SIZE - 1
        ) // CHUNK_SIZE

        print(
            f"Sending {total_packets} data packets..."
        )

        for packet_number in range(total_packets):

            start = packet_number * CHUNK_SIZE
            end = min(start + CHUNK_SIZE, firmware_size)

            chunk = firmware[start:end]

            packet = struct.pack(
                "<BIH",
                DATA,
                packet_number,
                len(chunk)
            ) + chunk

            success = send_packet_wait_ack(
                ser,
                packet,
                packet_number
            )

            if not success:
                print()
                print(
                    f"ERROR: Packet {packet_number} "
                    f"failed after {MAX_RETRIES} attempts."
                )
                return False

            progress = (end * 100) / firmware_size

            print(
                f"\rPacket {packet_number + 1}/{total_packets} "
                f"({progress:6.2f}%)",
                end="",
                flush=True
            )

        print()
        print()

        # ----------------------------------------------------
        # END packet
        # ----------------------------------------------------

        print("Sending END packet...")

        end_packet = struct.pack(
            "<B",
            END
        )

        if not send_packet_wait_ack(
            ser,
            end_packet,
            "END"
        ):
            print("ERROR: STM32 did not ACK END.")
            return False

        print("END ACK received.")
        print()

        print("Firmware transfer completed successfully.")

        return True

    finally:

        ser.close()


# ============================================================
# Main
# ============================================================

if __name__ == "__main__":

    parser = argparse.ArgumentParser(
        description="STM32L476 UART firmware uploader"
    )

    parser.add_argument(
        "port",
        help="Serial port, e.g. COM5 or /dev/ttyUSB0"
    )

    parser.add_argument(
        "firmware",
        help="Path to .bin firmware file"
    )

    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="UART baud rate (default: 115200)"
    )

    args = parser.parse_args()

    success = send_firmware(
        args.port,
        args.baud,
        args.firmware
    )

    if success:
        print("RESULT: PASS")
    else:
        print("RESULT: FAIL")