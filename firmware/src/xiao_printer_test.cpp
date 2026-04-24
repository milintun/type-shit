#include <Arduino.h>

// XIAO ESP32S3 — use Serial1 on any GPIO pins
// Default: TX=D6 (GPIO43), RX=D7 (GPIO44)
// Change these to match your wiring
#define PRINTER_TX 43
#define PRINTER_RX 44

void setup() {
  Serial.begin(115200);  // USB debug
  Serial1.begin(9600, SERIAL_8N1, PRINTER_RX, PRINTER_TX);

  delay(2000);

  Serial.println("Sending to printer...");
  Serial1.println("Thermal Printer Test");
  delay(500);
  Serial1.println("--------------------------");
  delay(500);
  Serial1.println("Hello from XIAO ESP32S3!");
  delay(500);

  // Feed paper
  Serial1.write(0x1B);
  Serial1.write('d');
  Serial1.write((uint8_t)4);

  Serial.println("Done!");
}

void loop() {
}
