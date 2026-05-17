# Wireless Printing — v6

## Status: Working (2026-05-13)

Full end-to-end wireless pipeline — no USB data cable needed between Mac and XIAO.

## How It Works

```
Browser (index-v6.html)
    ↕ HTTP (localhost:3000)
Node.js (wireless_server.js)
    ↕ HTTP (192.168.4.1)
XIAO ESP32S3 — WiFi Access Point (xiao_ap.cpp)
    ↕ Serial1 (9600 baud, GPIO 43/44)
Thermal Printer (QR204)
```

### Flow
1. Press physical button on XIAO
2. Browser polls `localhost:3000/button-state` every 150ms
3. Server proxies to XIAO `GET /button` → returns START or STOP
4. Browser records audio on START, stops on STOP
5. Pipeline runs: Whisper transcribe → Claude font pick → Imagen generation
6. Browser POSTs base64 image to `localhost:3000/print`
7. Server saves image, runs `wireless_print_bitmap.py`
8. Python converts image → ESC/POS bitmap, POSTs raw bytes to XIAO `POST /print`
9. XIAO forwards bytes to printer via Serial1
10. Printer prints, feeds blank lines

## Setup

### 1. Flash the XIAO
```bash
cd firmware
pio run -e xiao_ap -t upload
```
XIAO creates WiFi network **"typeshit"** (password: **typeshit123**).

### 2. Connect Mac to XIAO WiFi
- Join "typeshit" WiFi network
- Plug iPhone in via USB for internet (API calls need internet)
- Mac routes: internet → iPhone USB, XIAO → WiFi

### 3. Network priority (one-time)
Make sure iPhone USB is prioritized over WiFi for internet:
```bash
sudo networksetup -ordernetworkservices "iPhone USB USB" "Wi-Fi" "Thunderbolt Bridge" "USB 10/100/1G/2.5G LAN" "Arduino Leonardo 5" "Arduino Leonardo 4" "Arduino Leonardo 3" "USB JTAG/serial debug unit 2" "Arduino Leonardo" "Arduino Micro 2" "Arduino Micro" "LG Monitor Controls"
```

### 4. Start the server
```bash
node wireless_server.js
```
Open http://localhost:3000

### 5. Press the button and speak!

## Files

| File | Purpose |
|------|---------|
| `firmware/src/xiao_ap.cpp` | XIAO firmware — WiFi AP + HTTP server (/ping, /button, /print) |
| `wireless_server.js` | Node server — proxies button state, handles print pipeline |
| `index-v6.html` | Browser UI — polls button, records audio, runs AI pipeline |
| `firmware/wireless_print_bitmap.py` | Converts PNG → ESC/POS bitmap, POSTs to XIAO over WiFi |
| `.env` | API keys + `XIAO_IP=192.168.4.1` |

## Hardware Wiring

| XIAO Pin | Connection |
|----------|------------|
| D0 | LED (with resistor) |
| D1 | Button (INPUT_PULLUP) |
| GPIO 43 (TX) | Printer RX |
| GPIO 44 (RX) | Printer TX |
| GND | Printer serial GND (**direct — no resistor!**) |

**Important:** Printer serial GND must connect directly to XIAO GND. A resistor on the GND line corrupts serial communication.

## .env Config
```
OPENAI_API_KEY=...
ANTHROPIC_API_KEY=...
GEMINI_API_KEY=...
XIAO_IP=192.168.4.1
```

## Known Quirks
- `curl` from Mac terminal can't reach XIAO (routes through iPhone USB) — but Node.js `fetch` and the Python script (bound to 192.168.4.2) work fine
- Google Fonts won't load (no DNS on XIAO WiFi) — UI falls back to Georgia
- XIAO HTTP server is single-threaded — button polling can block print requests briefly
