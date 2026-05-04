/*
  Wokwi Custom SPI Chip example

  The chip implements a simple ROT13 letter substitution cipher:
  https://en.wikipedia.org/wiki/ROT13

  See https://docs.wokwi.com/chips-api/getting-started for more info about custom chips
*/

#include <Arduino.h>
#include <SPI.h>
#include <unity.h>

#define CS 10

void test_spi(void) {
  char buffer[] = "Uryyb, FCV! ";
  digitalWrite(CS, LOW);
  SPI.begin();
  SPI.transfer(buffer, strlen(buffer));
  SPI.end();
  digitalWrite(CS, HIGH);
  TEST_ASSERT_EQUAL_STRING("Hello, SPI!", buffer);
}

void setup() {
  Serial.begin(115200);
  pinMode(CS, OUTPUT);

  UNITY_BEGIN();
  RUN_TEST(test_spi);
  UNITY_END();
}

void loop() {
  delay(1000);
}
