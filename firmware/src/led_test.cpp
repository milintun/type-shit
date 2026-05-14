#include <Arduino.h>
const int BUTTON_PIN = D1;
const int LED_PIN    = D0;
// ── Button state tracking ───────────────────────────────────────────────────
bool lastButtonState = HIGH;
bool buttonState     = HIGH;
unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE_MS = 50;

// Track button events so the server can poll transitions
// IDLE = nothing happened, START = button just pressed, STOP = button just released
enum ButtonEvent { EVT_IDLE, EVT_START, EVT_STOP };
volatile ButtonEvent pendingEvent = EVT_IDLE;

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
}

void loop() {

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
