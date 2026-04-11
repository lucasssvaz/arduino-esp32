/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)

#include <functional>
#include "WString.h"
#include "BLETypes.h"
#include "BLEServer.h"
#include "BLEClient.h"
#include "BLECharacteristic.h"
#include "BLEDescriptor.h"
#include "BLEService.h"
#include "BLEAdvertising.h"
#include "BLEScan.h"
#include "BLESecurity.h"
#include "BLEAdvertisedDevice.h"
#include "BLEScanResults.h"
#include "BLERemoteService.h"
#include "BLERemoteCharacteristic.h"
#include "BLERemoteDescriptor.h"
#include "BLEAdvertisementData.h"
#include "BLEBeacon.h"
#include "BLEEddystone.h"
#include "BLEHIDDevice.h"

/**
 * @brief Global BLE singleton -- the entry point for all BLE operations.
 *
 * Follows the Arduino convention used by WiFi, Serial, Wire, etc.
 * The class is BLEClass; users interact via the global `BLE` object.
 *
 * Usage:
 * @code
 * #include <BLE.h>
 *
 * void setup() {
 *     BLE.begin("MyDevice");
 *     BLEServer server = BLE.createServer();
 *     // ...
 * }
 * @endcode
 */
class BLEClass {
public:
  // --- Lifecycle ---
  BTStatus begin(const String &deviceName = "");
  void end(bool releaseMemory = false);
  bool isInitialized() const;
  explicit operator bool() const;

  // --- Identity ---
  BTAddress getAddress() const;
  String getDeviceName() const;
  BTStatus setOwnAddressType(BTAddress::Type type);
  BTStatus setOwnAddress(const BTAddress &addr);

  // --- Factory / Singletons ---
  BLEServer createServer();
  BLEScan getScan();
  BLEAdvertising getAdvertising();
  BLESecurity getSecurity();
  BLEClient createClient();

  // --- Power ---
  void setPower(int8_t txPowerDbm);
  int8_t getPower() const;

  // --- MTU ---
  BTStatus setMTU(uint16_t mtu);
  uint16_t getMTU() const;

  // --- IRK retrieval (for Home Assistant, ESPresense, etc.) ---
  bool getLocalIRK(uint8_t irk[16]) const;
  String getLocalIRKString() const;
  String getLocalIRKBase64() const;
  bool getPeerIRK(const BTAddress &peer, uint8_t irk[16]) const;
  String getPeerIRKString(const BTAddress &peer) const;
  String getPeerIRKBase64(const BTAddress &peer) const;
  String getPeerIRKReverse(const BTAddress &peer) const;

  // --- Default PHY preference (BLE 5.0) ---
  BTStatus setDefaultPhy(BLEPhy txPhy, BLEPhy rxPhy);
  BTStatus getDefaultPhy(BLEPhy &txPhy, BLEPhy &rxPhy) const;

  // --- Whitelist ---
  BTStatus whiteListAdd(const BTAddress &address);
  BTStatus whiteListRemove(const BTAddress &address);
  bool isOnWhiteList(const BTAddress &address) const;

  // --- Advertising shortcuts ---
  BTStatus startAdvertising();
  BTStatus stopAdvertising();

  // --- Stack info ---
  enum class Stack { NimBLE, Bluedroid };
  Stack getStack() const;
  const char *getStackName() const;
  bool isHostedBLE() const;

  // --- Hosted BLE (ESP32-P4 via esp-hosted) ---
  BTStatus setPins(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t rst);

  // --- Custom event handlers (advanced/extension point) ---
  using RawEventHandler = std::function<int(void *event, void *arg)>;
  BTStatus setCustomGapHandler(RawEventHandler handler);
  BTStatus setCustomGattcHandler(RawEventHandler handler);
  BTStatus setCustomGattsHandler(RawEventHandler handler);

  BLEClass();
  ~BLEClass();
  BLEClass(const BLEClass &) = delete;
  BLEClass &operator=(const BLEClass &) = delete;
  BLEClass(BLEClass &&) = delete;
  BLEClass &operator=(BLEClass &&) = delete;

private:
  struct Impl;
  Impl *_impl;
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_BLE)
extern BLEClass BLE;
#endif

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
