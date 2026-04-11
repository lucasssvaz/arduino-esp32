/*
 * BLE UART Service Example -- New API
 *
 * Implements the Nordic UART Service (NUS) for bidirectional
 * serial communication over BLE. Data received via BLE is
 * printed to Serial; data from Serial is sent as notifications.
 *
 * Compatible with the nRF Toolbox UART app or similar tools.
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <BLE.h>

#define UART_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define UART_RX_CHAR_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define UART_TX_CHAR_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

bool deviceConnected = false;
BLECharacteristic txChar;

void onUARTConnect(BLEServer server, const BLEConnInfo &conn) {
  deviceConnected = true;
  Serial.printf("UART client connected: %s (MTU %d)\n",
    conn.getAddress().toString().c_str(), conn.getMTU());
  Serial.println("Type text in Serial Monitor to send via BLE.");
}

void onUARTDisconnect(BLEServer server, const BLEConnInfo &conn, uint8_t reason) {
  deviceConnected = false;
  Serial.printf("UART client disconnected (reason 0x%02X)\n", reason);
  Serial.println("Restarting advertising...");
  server.startAdvertising();
  Serial.println("Waiting for new connection...");
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== BLE UART Service Example ===");

  Serial.print("Initializing BLE... ");
  if (!BLE.begin("UART Service")) {
    Serial.println("FAILED!");
    return;
  }
  Serial.println("OK");

  Serial.print("Creating server... ");
  BLEServer server = BLE.createServer();
  if (!server) {
    Serial.println("FAILED!");
    return;
  }
  Serial.println("OK");
  server.onConnect(onUARTConnect);
  server.onDisconnect(onUARTDisconnect);

  Serial.print("Creating UART service... ");
  BLEService svc = server.createService(UART_SERVICE_UUID);
  if (!svc) {
    Serial.println("FAILED!");
    return;
  }
  Serial.println("OK");

  txChar = svc.createCharacteristic(UART_TX_CHAR_UUID, BLEProperty::Notify);
  BLECharacteristic rx = svc.createCharacteristic(UART_RX_CHAR_UUID, BLEProperty::Write);

  rx.onWrite([](BLECharacteristic chr, const BLEConnInfo &conn) {
    String data = chr.getStringValue();
    Serial.printf("[BLE -> Serial] %s\n", data.c_str());
  });

  Serial.print("Starting server... ");
  BTStatus status = server.start();
  if (!status) {
    Serial.printf("FAILED! (%s)\n", status.toString());
    return;
  }
  Serial.println("OK");

  Serial.print("Starting advertising... ");
  BLEAdvertising adv = server.getAdvertising();
  adv.addServiceUUID(UART_SERVICE_UUID);
  status = server.startAdvertising();
  if (!status) {
    Serial.printf("FAILED! (%s)\n", status.toString());
    return;
  }
  Serial.println("OK");

  Serial.println();
  Serial.println("UART service ready! Waiting for connections...");
  Serial.printf("Device: %s\n", BLE.getDeviceName().c_str());
  Serial.println("Connect with nRF Connect or nRF Toolbox UART app.");
  Serial.println();
}

void loop() {
  if (deviceConnected && txChar && Serial.available()) {
    String data = Serial.readStringUntil('\n');
    txChar.notify((const uint8_t *)data.c_str(), data.length());
    Serial.printf("[Serial -> BLE] %s\n", data.c_str());
  }
  delay(10);
}
