#!/usr/bin/env python3
"""Print bitmap image to QR204 thermal printer via USB, sending raw ESC/POS."""

import sys
import struct
import time
from PIL import Image
from escpos.printer import Usb

VENDOR_ID = 0x0485
PRODUCT_ID = 0x5741
PRINTER_WIDTH = 384


def image_to_bitmap(img_path):
    img = Image.open(img_path)

    # Handle transparency — paste onto white background
    if img.mode in ("RGBA", "LA", "P"):
        bg = Image.new("RGB", img.size, (255, 255, 255))
        if img.mode == "P":
            img = img.convert("RGBA")
        bg.paste(img, mask=img.split()[-1] if "A" in img.mode else None)
        img = bg

    ratio = PRINTER_WIDTH / img.width
    new_height = int(img.height * ratio)
    img = img.resize((PRINTER_WIDTH, new_height))
    img = img.convert("L")

    width_bytes = PRINTER_WIDTH // 8
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

    p = Usb(VENDOR_ID, PRODUCT_ID)

    # Set heat settings: ESC 7 maxHeatDots heatTime heatInterval
    # Lower maxHeatDots (first param) = fewer dots heated at once = less current
    p._raw(bytes([0x1B, 0x37, 4, 80, 20]))
    time.sleep(0.1)

    # GS v 0 header
    header = struct.pack("4B", 0x1D, 0x76, 0x30, 0x00)
    header += struct.pack("<H", row_bytes)
    header += struct.pack("<H", height)
    p._raw(header)
    time.sleep(0.1)

    # Send bitmap in small chunks
    chunk_size = 16
    for i in range(0, len(bitmap), chunk_size):
        try:
            p._raw(bytes(bitmap[i : i + chunk_size]))
        except Exception as e:
            print(f"  Error at byte {i}: {e}")
            time.sleep(1)
            p._raw(bytes(bitmap[i : i + chunk_size]))
        time.sleep(0.02)
        if i > 0 and i % 2400 == 0:
            print(f"  Row {i // row_bytes}/{height}...")

    time.sleep(1)
    p._raw(b"\x1b\x64\x08")
    time.sleep(0.5)
    p.close()
    print("Done!")


if __name__ == "__main__":
    main()
