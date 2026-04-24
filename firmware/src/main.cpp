#include <Arduino.h>

const int BUTTON_PIN = 7;
const int LED_PIN    = 5;

bool lastButtonState = HIGH;
bool buttonState     = HIGH;

unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE_MS = 50;

void setup() {
  Serial.begin(9600);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
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
