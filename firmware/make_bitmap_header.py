#!/usr/bin/env python3
"""Convert an image to a C header with ESC/POS bitmap bytes for direct firmware use.

Usage: python3 make_bitmap_header.py <image_path> [output_header]
  Default output: firmware/src/<stem>_bitmap.h
"""

import sys
import struct
from pathlib import Path
from PIL import Image

PRINTER_WIDTH = 384


def image_to_escpos(img_path):
    img = Image.open(img_path)

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
                if px < 160:
                    byte |= 1 << (7 - bit)
            bitmap.append(byte)

    # Heat settings matching the working USB print_bitmap.py
    heat_cmd = struct.pack("5B", 0x1B, 0x37, 8, 80, 60)
    header = struct.pack("4B", 0x1D, 0x76, 0x30, 0x00)
    header += struct.pack("<H", width_bytes)
    header += struct.pack("<H", height)
    feed_cmd = struct.pack("3B", 0x1B, 0x64, 0x08)  # ESC d 8 — feed 8 lines

    data = heat_cmd + header + bytes(bitmap) + feed_cmd
    print(f"  {PRINTER_WIDTH}x{height} px, {len(bitmap)} bitmap bytes, {len(data)} total")
    return data


def to_c_header(data, var_name, source_name):
    lines = [
        f"// Auto-generated from {source_name} — do not edit",
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        f"static const uint8_t {var_name}[] = {{",
    ]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    lines.append(f"static const size_t {var_name}_LEN = sizeof({var_name});")
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) < 2:
        print(f"Usage: python3 {sys.argv[0]} <image_path> [output_header]")
        sys.exit(1)

    img_path = Path(sys.argv[1])
    var_name = img_path.stem.upper().replace("-", "_").replace(" ", "_") + "_BITMAP"
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else \
        Path(__file__).parent / "src" / f"{img_path.stem}_bitmap.h"

    print(f"Converting {img_path}...")
    data = image_to_escpos(img_path)
    out_path.write_text(to_c_header(data, var_name, img_path.name))
    print(f"Written: {out_path}  ({len(data)} bytes as {var_name}[])")


if __name__ == "__main__":
    main()
