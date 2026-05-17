#include <Arduino.h>

const int BUTTON_PIN = D1;
const int LED_PIN    = D0;

enum State { IDLE, RECORDING, PROCESSING, PRINTING };

State state = IDLE;
unsigned long stateStart    = 0;
unsigned long lastLedToggle = 0;
bool ledState = false;

bool lastButtonState = HIGH;
bool buttonState     = HIGH;
unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE_MS = 50;

const unsigned long RECORD_MS  = 3000;
const unsigned long PROCESS_MS = 5000;
const unsigned long PRINT_MS   = 5000;
const unsigned long SLOW_MS    = 500;
const unsigned long FAST_MS    = 100;

void enterState(State s) {
  state      = s;
  stateStart = millis();
  ledState   = false;
  lastLedToggle = millis();
  switch (s) {
    case RECORDING:  digitalWrite(LED_PIN, HIGH); Serial.println("Recording...");   break;
    case PROCESSING: digitalWrite(LED_PIN, LOW);  Serial.println("Processing...");  break;
    case PRINTING:   digitalWrite(LED_PIN, LOW);  Serial.println("Printing...");    break;
    case IDLE:       digitalWrite(LED_PIN, LOW);  Serial.println("Done. Waiting."); break;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  buttonState     = digitalRead(BUTTON_PIN);
  lastButtonState = buttonState;

  Serial.println("Press button to start.");
}

void loop() {
  // Button debounce (PULLUP: pressed = LOW)
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) lastDebounce = millis();
  if ((millis() - lastDebounce) > DEBOUNCE_MS && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW && state == IDLE) {  // press edge, only from IDLE
      enterState(RECORDING);
    }
  }
  lastButtonState = reading;

  unsigned long elapsed = millis() - stateStart;
  unsigned long now     = millis();

  switch (state) {
    case IDLE: break;

    case RECORDING:
      if (elapsed >= RECORD_MS) enterState(PROCESSING);
      break;

    case PROCESSING:
      if (elapsed >= PROCESS_MS) { enterState(PRINTING); break; }
      if (now - lastLedToggle >= SLOW_MS) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        lastLedToggle = now;
      }
      break;

    case PRINTING:
      if (elapsed >= PRINT_MS) { enterState(IDLE); break; }
      if (now - lastLedToggle >= FAST_MS) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        lastLedToggle = now;
      }
      break;
  }
}
