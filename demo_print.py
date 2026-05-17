#!/usr/bin/env python3
"""Demo prototype: fake process + real print.

Usage: python3 demo_print.py <image_path>

Requires: Mac connected to XIAO's "typeshit" AP (192.168.4.1)
"""

import sys
import struct
import time
import socket
import json
from PIL import Image

XIAO_HOST = "192.168.4.1"
XIAO_PORT = 80
WIFI_SOURCE_IP = "192.168.4.2"
PRINTER_WIDTH = 384


# ── Image conversion (matches working print_bitmap.py exactly) ──────────────

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
                if px < 145:  # higher = more mid-tones print dark
                    byte |= 1 << (7 - bit)
            bitmap.append(byte)

    # Higher density but slow — longer delays between chunks let head cool
    heat_cmd = struct.pack("5B", 0x1B, 0x37, 11, 120, 40)
    header = struct.pack("4B", 0x1D, 0x76, 0x30, 0x00)
    header += struct.pack("<H", width_bytes)
    header += struct.pack("<H", height)

    payload = heat_cmd + header + bytes(bitmap)
    print(f"  Image: {PRINTER_WIDTH}x{height}, {len(payload)} bytes")
    return payload


# ── Raw HTTP helpers ────────────────────────────────────────────────────────

def raw_post(path, body, timeout=10):
    """POST raw bytes to XIAO, return response body."""
    sock = socket.create_connection(
        (XIAO_HOST, XIAO_PORT),
        timeout=timeout,
        source_address=(WIFI_SOURCE_IP, 0),
    )
    req = (
        f"POST {path} HTTP/1.0\r\n"
        f"Host: {XIAO_HOST}\r\n"
        f"Content-Length: {len(body)}\r\n"
        f"\r\n"
    ).encode() + body
    sock.sendall(req)

    sock.settimeout(timeout)
    resp = b""
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            resp += chunk
    except socket.timeout:
        pass
    sock.close()

    parts = resp.split(b"\r\n\r\n", 1)
    return parts[1].decode(errors="replace") if len(parts) > 1 else ""


def raw_get(path, timeout=10):
    """GET from XIAO, return response body."""
    sock = socket.create_connection(
        (XIAO_HOST, XIAO_PORT),
        timeout=timeout,
        source_address=(WIFI_SOURCE_IP, 0),
    )
    req = (
        f"GET {path} HTTP/1.0\r\n"
        f"Host: {XIAO_HOST}\r\n"
        f"\r\n"
    ).encode()
    sock.sendall(req)

    sock.settimeout(timeout)
    resp = b""
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            resp += chunk
    except socket.timeout:
        pass
    sock.close()

    parts = resp.split(b"\r\n\r\n", 1)
    return parts[1].decode(errors="replace") if len(parts) > 1 else ""


def set_led(mode):
    raw_post("/led", mode.encode())


def get_button():
    body = raw_get("/button")
    try:
        return json.loads(body)["state"]
    except (json.JSONDecodeError, KeyError):
        return "IDLE"


def send_print_chunked(data):
    """Send print data in 128-byte chunks via separate POSTs.

    Matches the working print_bitmap.py approach:
    128-byte chunks with 150ms delay between each.
    """
    chunk_size = 128
    total = len(data)
    sent = 0

    for i in range(0, total, chunk_size):
        chunk = data[i : i + chunk_size]

        result = raw_post("/print", chunk, timeout=10)

        # Check XIAO response
        try:
            resp = json.loads(result)
            forwarded = resp.get("bytes", 0)
            if forwarded != len(chunk):
                print(f"\n  WARNING: sent {len(chunk)} but XIAO forwarded {forwarded}")
        except (json.JSONDecodeError, KeyError):
            pass

        sent += len(chunk)

        # 500ms delay — lets print head cool between chunks
        time.sleep(0.5)

        if sent % 2048 < chunk_size or sent >= total:
            pct = sent * 100 // total
            sys.stdout.write(f"\r  Printing... {pct}%")
            sys.stdout.flush()

    print()

    # Feed paper — separate POST after 500ms delay (matches print_bitmap.py)
    time.sleep(0.5)
    raw_post("/print", b"\x1b\x64\x04")
    time.sleep(1)


# ── Main ────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print(f"Usage: python3 {sys.argv[0]} <image_path>")
        sys.exit(1)

    img_path = sys.argv[1]

    print(f"Loading {img_path}...")
    payload = image_to_escpos(img_path)
    print(f"Ready. {len(payload)} bytes.\n")

    try:
        raw_get("/ping")
        print("Connected to XIAO at 192.168.4.1")
    except Exception as e:
        print(f"Cannot reach XIAO: {e}")
        print("Make sure Mac is connected to 'typeshit' WiFi")
        sys.exit(1)

    set_led("off")

    while True:
        print("\nWaiting for button press...")
        while True:
            state = get_button()
            if state != "IDLE":
                break
            time.sleep(0.2)

        # Simulate recording (3s LED on)
        print("Recording...")
        set_led("on")
        time.sleep(3)

        # Fake processing (5s slow blink)
        print("Processing...")
        set_led("slow")
        time.sleep(5)

        # Print — chunked, matching working print_bitmap.py
        print("Printing...")
        set_led("fast")
        send_print_chunked(payload)

        set_led("off")
        print("Done!")


if __name__ == "__main__":
    main()
