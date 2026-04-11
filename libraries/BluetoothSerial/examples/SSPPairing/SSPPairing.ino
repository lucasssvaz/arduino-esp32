/*
 * BluetoothSerial SSP Pairing
 *
 * Demonstrates Secure Simple Pairing (SSP) with numeric comparison.
 * When a phone pairs, the passkey is printed on Serial. Type 'y' to accept.
 *
 * For legacy PIN pairing instead of SSP, replace enableSSP() with:
 *   SerialBT.disableSSP();
 *   SerialBT.setPin("1234");
 */

#include "BluetoothSerial.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

BluetoothSerial SerialBT;
bool confirmPending = false;

void setup() {
  Serial.begin(115200);

  SerialBT.enableSSP(false, true);

  SerialBT.onConfirmRequest([](uint32_t passkey) {
    Serial.printf("Confirm passkey: %06lu (y/n)? ", passkey);
    confirmPending = true;
  });

  SerialBT.onAuthComplete([](bool success) {
    if (success) {
      Serial.println("Pairing successful!");
    } else {
      Serial.println("Pairing failed.");
    }
  });

  BTStatus status = SerialBT.begin("ESP32-SSP");
  if (!status) {
    Serial.println("Bluetooth init failed!");
    while (true) delay(1000);
  }

  Serial.println("Bluetooth started with SSP. Pair with \"ESP32-SSP\"");
}

void loop() {
  if (confirmPending && Serial.available()) {
    char c = Serial.read();
    if (c == 'y' || c == 'Y') {
      SerialBT.confirmReply(true);
      Serial.println("Accepted.");
    } else {
      SerialBT.confirmReply(false);
      Serial.println("Rejected.");
    }
    confirmPending = false;
  }

  while (SerialBT.available()) {
    Serial.write(SerialBT.read());
  }
  while (Serial.available()) {
    SerialBT.write(Serial.read());
  }

  delay(1);
}

#pragma GCC diagnostic pop
