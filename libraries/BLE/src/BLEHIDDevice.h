/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2018 chegewara
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

#include "impl/BLEGuards.h"
#if BLE_ENABLED

#include "BLEServer.h"
#include "BLECharacteristic.h"
#include "BLEDescriptor.h"
#include "HIDTypes.h"

/**
 * @brief HID-over-GATT (HOGP) device helper.
 *
 * Creates and manages the standard GATT services for a BLE HID device:
 *   - Device Information Service (0x180A)
 *   - HID Service (0x1812)
 *   - Battery Service (0x180F)
 *
 * Usage:
 * @code
 * BLEServer server = BLE.createServer();
 * BLEHIDDevice hid(server);
 * hid.manufacturer("MyCompany");
 * hid.pnp(0x02, 0x1234, 0x5678, 0x0100);
 * hid.hidInfo(0x00, 0x01);
 * hid.reportMap(myDescriptor, sizeof(myDescriptor));
 * hid.startServices();
 * server.start();
 * @endcode
 */
class BLEHIDDevice {
public:
  explicit BLEHIDDevice(BLEServer server);
  ~BLEHIDDevice() = default;

  void reportMap(const uint8_t *map, uint16_t size);
  void startServices();

  BLEService deviceInfoService();
  BLEService hidService();
  BLEService batteryService();

  BLECharacteristic manufacturer();
  void manufacturer(const String &name);

  void pnp(uint8_t sig, uint16_t vendorId, uint16_t productId, uint16_t version);
  void hidInfo(uint8_t country, uint8_t flags);

  void setBatteryLevel(uint8_t level);

  BLECharacteristic hidControl();
  BLECharacteristic protocolMode();

  BLECharacteristic inputReport(uint8_t reportId);
  BLECharacteristic outputReport(uint8_t reportId);
  BLECharacteristic featureReport(uint8_t reportId);

  BLECharacteristic bootInput();
  BLECharacteristic bootOutput();

private:
  BLEServer _server;
  BLEService _devInfoSvc;
  BLEService _hidSvc;
  BLEService _batterySvc;

  BLECharacteristic _mfgChar;
  BLECharacteristic _pnpChar;
  BLECharacteristic _hidInfoChar;
  BLECharacteristic _reportMapChar;
  BLECharacteristic _hidControlChar;
  BLECharacteristic _protocolModeChar;
  BLECharacteristic _batteryLevelChar;
};

#endif /* BLE_ENABLED */
