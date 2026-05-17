/*
 * kick_print.cpp — standalone button-triggered bitmap printer
 *
 * No WiFi. Press button → LED fast-blinks → kick.png prints → LED on.
 *
 * Hardware:
 *   Button: D1 (INPUT_PULLUP, active LOW)
 *   LED:    D0
 *   Printer Serial1: TX=43, RX=44, 9600 baud
 */

#include <Arduino.h>
#include "kick_bitmap.h"

#define BUTTON_PIN D1
#define LED_PIN    D0
#define PRINTER_TX 43
#define PRINTER_RX 44

// Debounce
bool lastButtonState  = HIGH;
bool buttonState      = HIGH;
unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE_MS = 50;

void printBitmap() {
  digitalWrite(LED_PIN, HIGH);

  for (size_t i = 0; i < KICK_BITMAP_LEN; i++) {
    Serial1.write(KICK_BITMAP[i]);
    delayMicroseconds(1042);  // 9600 baud pacing

    // Blink LED and cooling pause every 64 bytes
    if (i % 64 == 63) {
      digitalWrite(LED_PIN, (i / 64) % 2 == 0 ? LOW : HIGH);
      delay(500);
    }
  }

  digitalWrite(LED_PIN, HIGH);
  Serial.println("Print done.");
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, PRINTER_RX, PRINTER_TX);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  buttonState     = digitalRead(BUTTON_PIN);
  lastButtonState = buttonState;

  Serial.println("kick_print ready — press button to print");
}

void loop() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounce = millis();
  }

  if ((millis() - lastDebounce) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {  // button pressed
        Serial.println("Button pressed — printing...");
        printBitmap();
      }
    }
  }

  lastButtonState = reading;
}
