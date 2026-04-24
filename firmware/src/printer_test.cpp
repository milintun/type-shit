#include <Arduino.h>

void setup()
{
  Serial1.begin(9600);
  Serial.begin(9600); // For debugging via Serial Monitor

//   delay(5000); // Wait for self-test to finish

//   Serial1.println("Thermal Printer Test");
//   delay(500);                                               
//   Serial1.println("--------------------------");
//   delay(500);                                                 
  Serial1.println("IM ALIVEEEE");
  Serial1.println("IM ALIVEEEE");
  Serial1.println("IM ALIVEEEE");

//   delay(500); 

  // Feed paper
  Serial1.write(0x1B); // ESC command
  Serial1.write('d');   // Feed command
  Serial1.write((uint8_t)3);    // Feed 4 lines
}

void loop()
{
}

