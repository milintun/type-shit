/*
 * wireless_main.cpp — Production WiFi firmware for XIAO ESP32S3
 *
 * Fully wireless: connects to WiFi, serves HTTP endpoints for button state
 * and printing. No USB serial needed (power via battery/wall adapter).
 *
 * Endpoints:
 *   GET  /ping   → {"ok":true}
 *   GET  /button → {"state":"IDLE"} / {"state":"START"} / {"state":"STOP"}
 *   POST /print  → receives raw ESC/POS bytes, forwards to Serial1 (thermal printer)
 *
 * mDNS: reachable at http://xiao-printer.local
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ── WiFi credentials ────────────────────────────────────────────────────────
// Update these for your network, or flash xiao_wifi_test first to verify
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// ── mDNS hostname ───────────────────────────────────────────────────────────
const char* MDNS_HOSTNAME = "xiao-printer";

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

// ── WiFi reconnection ───────────────────────────────────────────────────────
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_MS = 10000;

// ── HTTP server ─────────────────────────────────────────────────────────────
WebServer server(80);

// ── LED blink for status ────────────────────────────────────────────────────
void blinkLED(int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(ms);
    digitalWrite(LED_PIN, LOW);
    delay(ms);
  }
}

// ── Handlers ────────────────────────────────────────────────────────────────

void handlePing() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleButton() {
  server.sendHeader("Access-Control-Allow-Origin", "*");

  const char* state;
  switch (pendingEvent) {
    case EVT_START: state = "START"; break;
    case EVT_STOP:  state = "STOP";  break;
    default:        state = "IDLE";  break;
  }

  // Consume event after read
  pendingEvent = EVT_IDLE;

  String json = "{\"state\":\"";
  json += state;
  json += "\"}";
  server.send(200, "application/json", json);
}

void handlePrint() {
  server.sendHeader("Access-Control-Allow-Origin", "*");

  // Handle CORS preflight
  if (server.method() == HTTP_OPTIONS) {
    server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }

  const String& body = server.arg("plain");
  size_t len = body.length();

  // Forward raw bytes to printer
  for (size_t i = 0; i < len; i++) {
    Serial1.write((uint8_t)body[i]);
  }

  Serial.printf("Print: forwarded %d bytes to printer\n", (int)len);

  String json = "{\"ok\":true,\"bytes\":";
  json += len;
  json += "}";
  server.send(200, "application/json", json);
}

void handleCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

void handleNotFound() {
  // Handle CORS preflight for any route
  if (server.method() == HTTP_OPTIONS) {
    handleCORS();
    return;
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(404, "application/json", "{\"error\":\"not found\"}");
}

// ── Setup ───────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, PRINTER_RX, PRINTER_TX);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Connect to WiFi
  Serial.printf("\n[wireless_main] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.printf("ConnectADO! IP: %s\n", WiFi.localIP().toString().c_str());
    blinkLED(3, 100);  // 3 quick blinks = connected
  } else {
    Serial.println("\nWiFi connection failed! Will retry in loop...");
    blinkLED(10, 50);  // rapid blinks = failed
  }

  // Start mDNS
  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf("mDNS: http://%s.local\n", MDNS_HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("mDNS failed to start");
  }

  // Register endpoints
  server.on("/ping",   HTTP_GET,  handlePing);
  server.on("/button", HTTP_GET,  handleButton);
  server.on("/print",  HTTP_POST, handlePrint);
  server.on("/print",  HTTP_OPTIONS, handleCORS);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started on port 80");
  Serial.printf("Endpoints: GET /ping, GET /button, POST /print\n");
}

// ── Loop ────────────────────────────────────────────────────────────────────

void loop() {
  server.handleClient();

  // Periodic WiFi reconnection check
  if (millis() - lastWifiCheck > WIFI_CHECK_MS) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  // Button debounce
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounce = millis();
  }

  if ((millis() - lastDebounce) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == LOW) {
        digitalWrite(LED_PIN, HIGH);
        pendingEvent = EVT_START;
        Serial.println("Button: START");
      } else {
        digitalWrite(LED_PIN, LOW);
        pendingEvent = EVT_STOP;
        Serial.println("Button: STOP");
      }
    }
  }

  lastButtonState = reading;
}
