#include <Arduino.h>

// XIAO ESP32S3 pins — update to match your wiring
const int BUTTON_PIN = D1;
const int LED_PIN    = D0;

#define PRINTER_TX 43
#define PRINTER_RX 44

bool lastButtonState = HIGH;
bool buttonState     = HIGH;

unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE_MS = 50;

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600, SERIAL_8N1, PRINTER_RX, PRINTER_TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  // Forward USB serial data to printer
  while (Serial.available()) {
    Serial1.write(Serial.read());
  }

  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounce = millis();
  }

  if ((millis() - lastDebounce) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == LOW) {
        // button pressed down → start recording
        digitalWrite(LED_PIN, HIGH);
        Serial.println("STOP");
      } else {
        // button released → stop recording
        digitalWrite(LED_PIN, LOW);
        Serial.println("START");
      }
    }
  }

  lastButtonState = reading;
}
