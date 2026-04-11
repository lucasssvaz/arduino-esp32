/*
 * Free BT Memory Example -- New API
 *
 * Demonstrates two approaches to reclaim Bluetooth controller memory
 * when BLE is no longer needed:
 *
 *   1. BLE.end(true)           -- initialise BLE, do work, then release.
 *   2. Override btInUse()      -- prevent allocation at startup (no BLE
 *                                 usage at all, maximum RAM savings).
 *
 * This sketch uses approach 1: it starts BLE, reads the local address,
 * then calls BLE.end(true) to shut down the stack AND release the
 * controller memory. This is useful when BLE is only needed briefly
 * (e.g. for provisioning).
 *
 * For approach 2, uncomment the btInUse() override below and remove
 * all BLE code. The controller memory will never be allocated.
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <Arduino.h>
#include <BLE.h>

/*
 * Uncomment the following to use approach 2 (no BLE at all).
 * The BT controller memory is freed at startup.
 *
 *   bool btInUse() { return false; }
 */

void printHeap(const char *label) {
  Serial.printf("[%s] Free heap: %lu bytes\n", label, (unsigned long)ESP.getFreeHeap());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Free BT Memory Example\n");

  printHeap("Before BLE.begin");

  if (!BLE.begin("TempDevice")) {
    Serial.println("BLE init failed!");
    return;
  }

  printHeap("After BLE.begin");
  Serial.printf("Local address: %s\n\n", BLE.getAddress().toString().c_str());

  Serial.println("Releasing BLE and controller memory...");
  BLE.end(true);

  printHeap("After BLE.end(true)");
  Serial.println("\nBluetooth memory has been released.");
  Serial.println("BLE can no longer be used in this session.");
}

void loop() {
  delay(10000);
}
