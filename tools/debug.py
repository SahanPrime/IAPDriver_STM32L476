import serial
import struct
import zlib
import time
import sys

# ---- Protocol constants (must match uart_protocol.h exactly) ----
START_BYTE = 0xAA
CMD_START_UPDATE = 0x01
CMD_WRITE_CHUNK  = 0x02
CMD_END_UPDATE   = 0x03
CMD_ACK          = 0x04
CMD_NACK         = 0x05
CMD_APPLY_UPDATE = 0x06

CHUNK_SIZE = 256


def build_packet(cmd, payload=b""):
    length = len(payload)
    header = struct.pack(">BBH", START_BYTE, cmd, length)
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    return header + payload + struct.pack(">I", crc)


def send_and_wait_ack(ser, cmd, payload=b"", timeout=2.0, retries=5):
    pkt = build_packet(cmd, payload)
    last_resp = b""
    for attempt in range(1, retries + 1):
        ser.reset_input_buffer()
        ser.write(pkt)
        ser.timeout = timeout
        resp = ser.read(8)   # ACK/NACK packets are fixed 8 bytes per send_ack()/send_nack()
        last_resp = resp
        if len(resp) < 2:
            print(f"  (attempt {attempt}/{retries}) no response for cmd 0x{cmd:02X}, retrying...")
            continue
        if resp[1] == CMD_ACK:
            return True
        elif resp[1] == CMD_NACK:
            return False
        print(f"  (attempt {attempt}/{retries}) unexpected response for cmd 0x{cmd:02X}: {resp!r}, retrying...")

    raise TimeoutError(f"No valid response for cmd 0x{cmd:02X} after {retries} attempts (last: {len(last_resp)} bytes: {last_resp!r})")


def send_update(port, baud, filepath):
    with open(filepath, "rb") as f:
        data = f.read()

    total_size = len(data)
    final_crc = zlib.crc32(data) & 0xFFFFFFFF

    # pad to multiple of 8 (double-word) as staging_write() requires
    pad_len = (-len(data)) % 8
    padded_data = data + b"\x00" * pad_len

    ser = serial.Serial(port, baud, timeout=2)
    time.sleep(0.5)  # let the port settle

    print(f"Sending START_UPDATE: size={total_size}, crc=0x{final_crc:08X}")
    start_payload = struct.pack(">II", total_size, final_crc)
    if not send_and_wait_ack(ser, CMD_START_UPDATE, start_payload):
        print("START_UPDATE NACKed")
        ser.close()
        return

    offset = 0
    chunk_num = 0
    while offset < len(padded_data):
        chunk = padded_data[offset:offset + CHUNK_SIZE]
        if len(chunk) % 8 != 0:
            chunk += b"\x00" * ((-len(chunk)) % 8)

        ok = send_and_wait_ack(ser, CMD_WRITE_CHUNK, chunk)
        while not ok:
            print(f"Chunk {chunk_num} NACKed, resending...")
            ok = send_and_wait_ack(ser, CMD_WRITE_CHUNK, chunk)

        offset += len(chunk)
        chunk_num += 1
        if chunk_num % 50 == 0:
            print(f"  {offset}/{len(padded_data)} bytes sent")

    print(f"  {offset}/{len(padded_data)} bytes sent (done)")
    print("Sending END_UPDATE")
    if not send_and_wait_ack(ser, CMD_END_UPDATE):
        print("END_UPDATE NACKed - CRC mismatch likely")
        ser.close()
        return

    print("Update staged successfully. Sending APPLY_UPDATE...")
    send_and_wait_ack(ser, CMD_APPLY_UPDATE)
    print("Device should now reset and apply the update.")

    ser.close()


if __name__ == "__main__":
    if len(sys.argv) != 5 or sys.argv[1] != "send":
        print("Usage: python debug.py send <PORT> <BAUD> <path_to_bin>")
        sys.exit(1)

    port = sys.argv[2]
    baud = int(sys.argv[3])
    filepath = sys.argv[4]

    send_update(port, baud, filepath)