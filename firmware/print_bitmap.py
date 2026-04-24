#!/usr/bin/env python3
"""Send a bitmap image to the QR204 thermal printer via Arduino serial passthrough."""

import sys
import os
import struct
import time
from PIL import Image

SERIAL_PORT = "/dev/cu.usbmodem1101"
PRINTER_WIDTH = 384  # 58mm printer = 384 dots wide


def image_to_escpos(img_path):
    """Convert image to ESC/POS bitmap bytes."""
    img = Image.open(img_path)

    # Handle transparency — paste onto white background
    if img.mode in ("RGBA", "LA", "P"):
        bg = Image.new("RGB", img.size, (255, 255, 255))
        if img.mode == "P":
            img = img.convert("RGBA")
        bg.paste(img, mask=img.split()[-1] if "A" in img.mode else None)
        img = bg

    # Resize to printer width, maintain aspect ratio
    ratio = PRINTER_WIDTH / img.width
    new_height = int(img.height * ratio)
    img = img.resize((PRINTER_WIDTH, new_height))

    # Convert to grayscale
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
                if px < 128:  # Dark pixel = print
                    byte |= 1 << (7 - bit)
            bitmap.append(byte)

    # Build GS v 0 command header
    header = struct.pack("4B", 0x1D, 0x76, 0x30, 0x00)
    header += struct.pack("<H", width_bytes)
    header += struct.pack("<H", height)

    print(f"  Image: {PRINTER_WIDTH}x{height}, {len(bitmap)} bitmap bytes")
    return header + bytes(bitmap)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: python {sys.argv[0]} <image_path>")
        sys.exit(1)

    img_path = sys.argv[1]
    print(f"Converting {img_path}...")
    data = image_to_escpos(img_path)
    print(f"Total data: {len(data)} bytes")

    print(f"Sending to {SERIAL_PORT}...")
    fd = os.open(SERIAL_PORT, os.O_WRONLY | os.O_NOCTTY)
    time.sleep(2)

    # Send data in chunks
    chunk_size = 128
    for i in range(0, len(data), chunk_size):
        os.write(fd, data[i : i + chunk_size])
        time.sleep(0.15)  # 128 bytes at 9600 baud = ~133ms
        if i > 0 and i % 2000 == 0:
            print(f"  Sent {i}/{len(data)} bytes...")

    # Feed paper
    time.sleep(0.5)
    os.write(fd, b"\x1b\x64\x04")
    time.sleep(1)

    os.close(fd)
    print("Done!")


if __name__ == "__main__":
    main()
