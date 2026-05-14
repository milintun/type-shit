/*
 * xiao_ap.cpp — XIAO ESP32S3 as Access Point
 *
 * XIAO creates its own WiFi network "typeshit". Your Mac joins it.
 * Mac gets internet from iPhone USB. No router needed.
 *
 * Default AP IP: 192.168.4.1
 *
 * Endpoints:
 *   GET  /ping   → {"ok":true}
 *   GET  /button → {"state":"IDLE"} / {"state":"START"} / {"state":"STOP"}
 *   POST /print  → receives raw ESC/POS bytes, forwards to Serial1
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>

// ── Access Point credentials ────────────────────────────────────────────────
const char* AP_SSID     = "typeshit";
const char* AP_PASSWORD = "typeshit123";  // min 8 chars for WPA2

// ── Hardware pins ───────────────────────────────────────────────────────────
const int BUTTON_PIN = D1;
const int LED_PIN    = D0;
#define PRINTER_TX 43
#define PRINTER_RX 44

// ── Button state tracking ───────────────────────────────────────────────────
bool lastButtonState = HIGH;
bool buttonState     = HIGH;
unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE_MS = 50;

enum ButtonEvent { EVT_IDLE, EVT_START, EVT_STOP };
volatile ButtonEvent pendingEvent = EVT_IDLE;

// ── LED blink modes (non-blocking) ──────────────────────────────────────────
enum LedMode { LED_OFF, LED_ON, LED_SLOW_BLINK, LED_FAST_BLINK, LED_ERROR };
LedMode ledMode = LED_OFF;
unsigned long lastLedToggle = 0;
unsigned long ledModeStartTime = 0;  // when current blink mode started
bool ledState = false;
bool printing = false;  // lock LED during printing — button can't override
const unsigned long LED_TIMEOUT_MS = 60000;  // 1 minute fallback
int errorBlinkCount = 0;  // counts toggles for error pattern (6 toggles = 3 blinks)

void updateLed() {
  unsigned long now = millis();
  // Fallback: stop blinking after 1 minute
  if ((ledMode == LED_SLOW_BLINK || ledMode == LED_FAST_BLINK || ledMode == LED_ERROR) &&
      (now - ledModeStartTime > LED_TIMEOUT_MS)) {
    ledMode = LED_OFF;
    printing = false;
    Serial.println("LED: timeout fallback → off");
  }
  switch (ledMode) {
    case LED_OFF:
      if (ledState) { digitalWrite(LED_PIN, LOW); ledState = false; }
      break;
    case LED_ON:
      if (!ledState) { digitalWrite(LED_PIN, HIGH); ledState = true; }
      break;
    case LED_SLOW_BLINK:
      if (now - lastLedToggle >= 500) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        lastLedToggle = now;
      }
      break;
    case LED_FAST_BLINK:
      if (now - lastLedToggle >= 100) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        lastLedToggle = now;
      }
      break;
    case LED_ERROR:
      if (now - lastLedToggle >= 150) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        lastLedToggle = now;
        errorBlinkCount++;
        if (errorBlinkCount >= 10) {  // 5 blinks (10 toggles) then off
          ledMode = LED_OFF;
          printing = false;
          errorBlinkCount = 0;
        }
      }
      break;
  }
}

// ── Raw TCP server on port 80 ───────────────────────────────────────────────
WiFiServer httpServer(80);

// ── HTTP response helpers ───────────────────────────────────────────────────
void sendResponse(WiFiClient& client, int code, const char* contentType, const String& body) {
  client.printf("HTTP/1.1 %d OK\r\n", code);
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type");
  client.printf("Content-Type: %s\r\n", contentType);
  client.printf("Content-Length: %d\r\n", body.length());
  client.println("Connection: close");
  client.println();
  client.print(body);
}

// ── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  delay(3000);
  Serial.println("BOOT: xiao_ap starting...");
  Serial1.begin(9600, SERIAL_8N1, PRINTER_RX, PRINTER_TX);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Start as Access Point
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4);

  Serial.println();
  Serial.printf("AP started: \"%s\"\n", AP_SSID);
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());

  httpServer.begin();
  Serial.println("HTTP server started on port 80");
  Serial.println("Endpoints: GET /ping, GET /button, POST /print");
  Serial.println();
  Serial.println("1. Connect Mac to WiFi \"typeshit\" (password: typeshit123)");
  Serial.println("2. curl http://192.168.4.1/ping");

  // Initialize button state from actual pin — prevents false trigger on boot
  buttonState = digitalRead(BUTTON_PIN);
  lastButtonState = buttonState;
  ledMode = LED_OFF;
}

// ── Loop ────────────────────────────────────────────────────────────────────
void loop() {
  // ── Handle HTTP clients ─────────────────────────────────────────────────
  WiFiClient client = httpServer.accept();
  if (client) {
    // Read the request line
    String requestLine = "";
    unsigned long start = millis();
    while (client.connected() && millis() - start < 5000) {
      if (client.available()) {
        char c = client.read();
        requestLine += c;
        if (c == '\n') break;
      }
    }
    requestLine.trim();

    // Parse method and path
    String method = requestLine.substring(0, requestLine.indexOf(' '));
    String path = requestLine.substring(requestLine.indexOf(' ') + 1);
    path = path.substring(0, path.indexOf(' '));

    // Read headers to get Content-Length
    int contentLength = 0;
    String headerLine = "";
    while (client.connected() && millis() - start < 5000) {
      if (client.available()) {
        char c = client.read();
        headerLine += c;
        if (c == '\n') {
          if (headerLine.length() <= 2) break; // empty line = end of headers
          if (headerLine.startsWith("Content-Length:") || headerLine.startsWith("content-length:")) {
            contentLength = headerLine.substring(headerLine.indexOf(':') + 1).toInt();
          }
          headerLine = "";
        }
      }
    }

    Serial.printf("[%s] %s (body: %d bytes)\n", method.c_str(), path.c_str(), contentLength);

    // ── Route: OPTIONS (CORS preflight) ─────────────────────────────────
    if (method == "OPTIONS") {
      sendResponse(client, 204, "text/plain", "");
    }
    // ── Route: GET /ping ────────────────────────────────────────────────
    else if (method == "GET" && path == "/ping") {
      sendResponse(client, 200, "application/json", "{\"ok\":true}");
    }
    // ── Route: GET /button ──────────────────────────────────────────────
    else if (method == "GET" && path == "/button") {
      const char* state;
      switch (pendingEvent) {
        case EVT_START: state = "START"; break;
        case EVT_STOP:  state = "STOP";  break;
        default:        state = "IDLE";  break;
      }
      pendingEvent = EVT_IDLE;

      String json = "{\"state\":\"";
      json += state;
      json += "\"}";
      sendResponse(client, 200, "application/json", json);
    }
    // ── Route: POST /led ───────────────────────────────────────────────
    else if (method == "POST" && path == "/led") {
      // Read body to get mode
      String body = "";
      int received = 0;
      while (received < contentLength && client.connected() && millis() - start < 5000) {
        if (client.available()) {
          body += (char)client.read();
          received++;
        }
      }
      body.trim();

      if (body == "off")        { ledMode = LED_OFF; printing = false; }
      else if (body == "on")    { ledMode = LED_ON; printing = false; }
      else if (body == "slow")  { ledMode = LED_SLOW_BLINK; printing = false; ledModeStartTime = millis(); }
      else if (body == "fast")  { ledMode = LED_FAST_BLINK; printing = true; ledModeStartTime = millis(); }
      else if (body == "error") { ledMode = LED_ERROR; printing = false; errorBlinkCount = 0; ledModeStartTime = millis(); }

      Serial.printf("LED: %s\n", body.c_str());
      sendResponse(client, 200, "application/json", "{\"ok\":true}");
    }
    // ── Route: POST /print ──────────────────────────────────────────────
    else if (method == "POST" && path == "/print") {
      ledMode = LED_FAST_BLINK;
      printing = true;
      ledModeStartTime = millis();  // start timeout

      // Read raw body bytes and forward to printer
      int received = 0;
      while (received < contentLength && client.connected() && millis() - start < 30000) {
        if (client.available()) {
          uint8_t b = client.read();
          Serial1.write(b);
          delayMicroseconds(1042);  // pace to 9600 baud — prevents print head heat spikes
          received++;
        }
        updateLed();  // keep LED blinking smoothly during transfer
      }

      Serial.printf("Print: forwarded %d bytes to Serial1\n", received);

      String json = "{\"ok\":true,\"bytes\":";
      json += received;
      json += "}";
      sendResponse(client, 200, "application/json", json);
    }
    // ── 404 ─────────────────────────────────────────────────────────────
    else {
      sendResponse(client, 404, "application/json", "{\"error\":\"not found\"}");
    }

    client.stop();
  }

  // ── Button debounce ─────────────────────────────────────────────────────
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounce = millis();
  }

  if ((millis() - lastDebounce) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == LOW) {
        if (!printing) { ledMode = LED_SLOW_BLINK; ledModeStartTime = millis(); }
        pendingEvent = EVT_STOP;
        Serial.println("Button: STOP");
      } else {
        printing = false;
        ledMode = LED_ON;
        ledModeStartTime = millis();
        pendingEvent = EVT_START;
        Serial.println("Button: START");
      }
    }
  }

  lastButtonState = reading;

  // ── Non-blocking LED update ─────────────────────────────────────────────
  updateLed();
}
