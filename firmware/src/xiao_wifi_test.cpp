/*
 * xiao_wifi_test.cpp — WiFi connectivity test for XIAO ESP32S3
 *
 * Phases 1-3: WiFi connect, button over HTTP, print over HTTP
 *
 * Endpoints:
 *   GET  /ping   → {"ok":true}
 *   GET  /button → {"state":"IDLE"} / {"state":"START"} / {"state":"STOP"}
 *   POST /print  → receives raw ESC/POS bytes, forwards to Serial1
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ── WiFi credentials (hardcoded for testing) ────────────────────────────────
const char* WIFI_SSID     = "typeshit";
const char* WIFI_PASSWORD = "typeshit";

// ── Hardware pins (same as xiao_main.cpp) ───────────────────────────────────
const int BUTTON_PIN = D1;
const int LED_PIN    = D0;
#define PRINTER_TX 43
#define PRINTER_RX 44

// ── Button state tracking ───────────────────────────────────────────────────
bool lastButtonState = HIGH;
bool buttonState     = HIGH;
unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE_MS = 50;

// Track button events so the server can poll transitions
// IDLE = nothing happened, START = button just pressed, STOP = button just released
enum ButtonEvent { EVT_IDLE, EVT_START, EVT_STOP };
volatile ButtonEvent pendingEvent = EVT_IDLE;

// ── HTTP server on port 80 ──────────────────────────────────────────────────
WebServer server(80);

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

  // Clear the event after it's been read (consumed by poll)
  pendingEvent = EVT_IDLE;

  String json = "{\"state\":\"";
  json += state;
  json += "\"}";
  server.send(200, "application/json", json);
}

void handlePrint() {
  server.sendHeader("Access-Control-Allow-Origin", "*");

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }

  const String& body = server.arg("plain");
  size_t len = body.length();

  // Write raw bytes to printer via Serial1
  for (size_t i = 0; i < len; i++) {
    Serial1.write((uint8_t)body[i]);
  }

  Serial.printf("Print: forwarded %d bytes to Serial1\n", (int)len);

  String json = "{\"ok\":true,\"bytes\":";
  json += len;
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(404, "application/json", "{\"error\":\"not found\"}");
}

// ── Setup ───────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, PRINTER_RX, PRINTER_TX);

  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Scan until target network is visible
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(500);
  WiFi.mode(WIFI_STA);
  delay(2000);
  bool found = false;
  while (!found) {
    Serial.println("\nScanning...");
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      Serial.printf("  %s (%d dBm)\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
      if (WiFi.SSID(i) == WIFI_SSID) found = true;
    }
    WiFi.scanDelete();
    if (!found) {
      Serial.printf("'%s' not found, rescanning...\n", WIFI_SSID);
      delay(2000);
    }
  }
  Serial.printf("Found '%s', connecting...\n", WIFI_SSID);

  // Full WiFi reset before connecting
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.mode(WIFI_STA);

  // Let DHCP assign IP (Mac hotspot handles this fine)
  // IPAddress local(172, 20, 10, 5);
  // IPAddress gateway(172, 20, 10, 1);
  // IPAddress subnet(255, 255, 255, 240);
  // WiFi.config(local, gateway, subnet);

  Serial.printf("\nConnecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++attempts >= 80) { // 40s timeout
      Serial.println();
      Serial.printf("WiFi failed. Status code: %d\n", WiFi.status());
      Serial.println("Check: hotspot on? SSID/password correct?");
      return;
    }
  }

  Serial.println();
  Serial.printf("Connectado! IP: %s\n", WiFi.localIP().toString().c_str());

  // Register HTTP endpoints
  server.on("/ping",   HTTP_GET,  handlePing);
  server.on("/button", HTTP_GET,  handleButton);
  server.on("/print",  HTTP_POST, handlePrint);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started on port 80");
  Serial.println("Endpoints: GET /ping, GET /button, POST /print");
}

// ── Loop ────────────────────────────────────────────────────────────────────

void loop() {
  server.handleClient();

  bool pressed = digitalRead(BUTTON_PIN);
  digitalWrite(LED_PIN, pressed);

  if (pressed != buttonState) {
    buttonState = pressed;
    if (pressed) {
      pendingEvent = EVT_START;
      Serial.println("Button: START");
    } else {
      pendingEvent = EVT_STOP;
      Serial.println("Button: STOP");
    }
  }
}
