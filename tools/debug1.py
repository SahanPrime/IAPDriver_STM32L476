import serial
import time

PORT = "COM3"
BAUD = 115200

packet = bytes.fromhex("aa0100080000621cd840ccf222881c7f")

with serial.Serial(PORT, BAUD, timeout=5) as ser:
    time.sleep(0.5)
    print(f"Sending {len(packet)}-byte START_UPDATE packet...")
    ser.write(packet)

    time.sleep(3)   # generous window - flash erase can take a while
    raw = ser.read(ser.in_waiting or 1)
    print("Raw bytes received:", raw)
    print("Hex:", raw.hex())