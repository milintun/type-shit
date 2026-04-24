#!/usr/bin/env python3
"""Send a bitmap image to the QR204 thermal printer via Arduino serial passthrough.
Uses GS v 0 with byte-at-a-time sending to prevent buffer overflow."""

import sys
import os
import struct
import time
from PIL import Image

SERIAL_PORT = "/dev/cu.usbmodem1101"
PRINTER_WIDTH = 384
BAUD_RATE = 9600
BYTE_TIME = 11.0 / BAUD_RATE  # ~1.15ms per byte


def image_to_bitmap(img_path):
    img = Image.open(img_path)

    # Resize to printer width
    ratio = PRINTER_WIDTH / img.width
    new_height = int(img.height * ratio)
    img = img.resize((PRINTER_WIDTH, new_height))

    img = img.convert("L")

    width_bytes = PRINTER_WIDTH // 8  # 48
    height = img.height
    pixels = list(img.getdata())

    bitmap = bytearray()
    for y in range(height):
        for x_byte in range(width_bytes):
            byte = 0
            for bit in range(8):
                x = x_byte * 8 + bit
                px = pixels[y * PRINTER_WIDTH + x]
                if px < 128:
                    byte |= 1 << (7 - bit)
            bitmap.append(byte)

    return bitmap, width_bytes, height


def main():
    if len(sys.argv) < 2:
        print(f"Usage: python {sys.argv[0]} <image_path>")
        sys.exit(1)

    img_path = sys.argv[1]
    print(f"Converting {img_path}...")
    bitmap, row_bytes, height = image_to_bitmap(img_path)
    print(f"  Image: {PRINTER_WIDTH}x{height}, {len(bitmap)} bytes")

    print(f"Sending to {SERIAL_PORT}...")
    fd = os.open(SERIAL_PORT, os.O_WRONLY | os.O_NOCTTY)
    time.sleep(2)

    # GS v 0 header
    header = struct.pack("4B", 0x1D, 0x76, 0x30, 0x00)
    header += struct.pack("<H", row_bytes)
    header += struct.pack("<H", height)

    # Send header
    os.write(fd, header)
    time.sleep(0.1)

    # Send bitmap one byte at a time with baud-rate pacing
    for i in range(len(bitmap)):
        os.write(fd, bytes([bitmap[i]]))
        time.sleep(BYTE_TIME)
        if i > 0 and i % (row_bytes * 50) == 0:
            print(f"  Row {i // row_bytes}/{height}...")

    # Feed paper
    time.sleep(1)
    os.write(fd, b"\x1b\x64\x04")
    time.sleep(1)

    os.close(fd)
    print("Done!")


if __name__ == "__main__":
    main()
