#!/usr/bin/env python3
"""Send a bitmap image to the thermal printer via XIAO WiFi HTTP endpoint.

Usage: python3 wireless_print_bitmap.py <image_path> [xiao_url]

Converts image to ESC/POS bitmap bytes and POSTs to XIAO's /print endpoint.
"""

import sys
import struct
import time
import socket
import http.client
import urllib.request
from PIL import Image

# WiFi interface source IP — forces traffic through WiFi, not iPhone USB
WIFI_SOURCE_IP = "192.168.4.2"

PRINTER_WIDTH = 384  # 58mm printer = 384 dots wide
DEFAULT_XIAO_URL = "http://xiao-printer.local"


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

    # Heat settings: ESC 7 maxHeatDots heatTime heatInterval
    # Conservative: fewer dots at once, shorter burn, longer cooldown
    heat_cmd = struct.pack("5B", 0x1B, 0x37, 7, 80, 50)

    # Build GS v 0 command header
    header = struct.pack("4B", 0x1D, 0x76, 0x30, 0x00)
    header += struct.pack("<H", width_bytes)
    header += struct.pack("<H", height)

    print(f"  Image: {PRINTER_WIDTH}x{height}, {len(bitmap)} bitmap bytes")
    return heat_cmd + header + bytes(bitmap)


def make_bound_connection(host, port):
    """Create an HTTP connection bound to the WiFi interface IP."""
    conn = http.client.HTTPConnection(host, port, timeout=10)
    # Override socket creation to bind to WiFi IP
    original_connect = conn.connect
    def bound_connect():
        conn.sock = socket.create_connection(
            (host, port),
            timeout=10,
            source_address=(WIFI_SOURCE_IP, 0),
        )
    conn.connect = bound_connect
    return conn


def send_to_xiao(data, xiao_url):
    """POST raw ESC/POS bytes to XIAO's /print endpoint over HTTP.

    Sends in chunks with delays to match 9600 baud printer timing.
    Binds to WiFi interface to avoid routing through iPhone USB.
    """
    # Parse host from URL
    host = xiao_url.replace("http://", "").rstrip("/")
    port = 80
    if ":" in host:
        host, port_str = host.rsplit(":", 1)
        port = int(port_str)

    # 128-byte chunks: small enough that XIAO's serial buffer never lags behind.
    # Delay = bytes * 0.0015s ≈ slightly slower than 9600 baud (1/960 ≈ 0.00104s/byte)
    # so each chunk fully forwards to the printer before the next arrives.
    chunk_size = 128
    total = len(data)

    print(f"Sending {total} bytes to {xiao_url}/print in {chunk_size}-byte chunks...")
    print(f"  Binding to {WIFI_SOURCE_IP} (WiFi interface)")

    for i in range(0, total, chunk_size):
        chunk = data[i : i + chunk_size]
        try:
            conn = make_bound_connection(host, port)
            conn.request("POST", "/print", body=chunk,
                         headers={"Content-Type": "application/octet-stream"})
            resp = conn.getresponse()
            resp.read()
            conn.close()
        except Exception as e:
            print(f"  Error at byte {i}: {e}")
            return False

        delay = len(chunk) * 0.0015
        time.sleep(delay)

        if i > 0 and i % 2000 == 0:
            print(f"  Sent {i}/{total} bytes...")

    # Send paper feed — give printer time to finish the last bitmap row
    time.sleep(1.5)
    try:
        feed_cmd = b"\x1b\x64\x08"  # ESC d 8 — feed 8 lines
        conn = make_bound_connection(host, port)
        conn.request("POST", "/print", body=feed_cmd,
                     headers={"Content-Type": "application/octet-stream"})
        resp = conn.getresponse()
        resp.read()
        conn.close()
    except Exception as e:
        print(f"  Feed command error: {e}")

    return True


def main():
    if len(sys.argv) < 2:
        print(f"Usage: python3 {sys.argv[0]} <image_path> [xiao_url]")
        print(f"  Default XIAO URL: {DEFAULT_XIAO_URL}")
        sys.exit(1)

    img_path = sys.argv[1]
    xiao_url = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_XIAO_URL

    print(f"Converting {img_path}...")
    data = image_to_escpos(img_path)
    print(f"Total data: {len(data)} bytes")

    print(f"Sending to XIAO at {xiao_url}...")
    success = send_to_xiao(data, xiao_url)

    if success:
        print("Done!")
    else:
        print("Print failed!")
        sys.exit(1)


if __name__ == "__main__":
    main()
