#include <Arduino.h>

#define PRINTER_TX 43
#define PRINTER_RX 44

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600, SERIAL_8N1, PRINTER_RX, PRINTER_TX);
}

void loop() {
  if (Serial.available()) {
    Serial1.write(Serial.read());
  }
  if (Serial1.available()) {
    Serial.write(Serial1.read());
  }
}
