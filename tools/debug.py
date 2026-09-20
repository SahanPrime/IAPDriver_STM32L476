import serial
import time

PORT = "COM3"   # change to yours
BAUD = 115200

with serial.Serial(PORT, BAUD, timeout=2) as ser:
    time.sleep(0.5)
    packet = bytes([0xAA, 0x01, 0x00, 0x08,
                     0x00, 0x00, 0x61, 0x94,   # dummy size+crc, doesn't matter for this test
                     0x00, 0x00, 0x00, 0x00])
    print("Sending test packet...")
    ser.write(packet)

    time.sleep(2)   # give the device time to respond/print
    raw = ser.read(ser.in_waiting or 1)
    print("Raw bytes received:", raw)
    print("As text:", raw.decode(errors="replace"))