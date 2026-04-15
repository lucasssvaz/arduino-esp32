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

#include "impl/BLEGuards.h"
#if BLE_ENABLED

#include "BLE.h"

#include <cstring>

static const BLEUUID kDevInfoSvcUUID(static_cast<uint16_t>(0x180A));
static const BLEUUID kHIDSvcUUID(static_cast<uint16_t>(0x1812));
static const BLEUUID kBatterySvcUUID(static_cast<uint16_t>(0x180F));

static const BLEUUID kMfgNameUUID(static_cast<uint16_t>(0x2A29));
static const BLEUUID kPnpIdUUID(static_cast<uint16_t>(0x2A50));
static const BLEUUID kHIDInfoUUID(static_cast<uint16_t>(0x2A4A));
static const BLEUUID kReportMapUUID(static_cast<uint16_t>(0x2A4B));
static const BLEUUID kHIDControlUUID(static_cast<uint16_t>(0x2A4C));
static const BLEUUID kProtocolModeUUID(static_cast<uint16_t>(0x2A4E));
static const BLEUUID kReportUUID(static_cast<uint16_t>(0x2A4D));
static const BLEUUID kBatteryLevelUUID(static_cast<uint16_t>(0x2A19));
static const BLEUUID kBootInputUUID(static_cast<uint16_t>(0x2A22));
static const BLEUUID kBootOutputUUID(static_cast<uint16_t>(0x2A32));

static const BLEUUID kReportRefDescUUID(static_cast<uint16_t>(0x2908));

BLEHIDDevice::BLEHIDDevice(BLEServer server) : _server(server) {
  _devInfoSvc = _server.createService(kDevInfoSvcUUID);
  _hidSvc = _server.createService(kHIDSvcUUID, 40);
  _batterySvc = _server.createService(kBatterySvcUUID);

  _mfgChar = _devInfoSvc.createCharacteristic(kMfgNameUUID, BLEProperty::Read);
  _pnpChar = _devInfoSvc.createCharacteristic(kPnpIdUUID, BLEProperty::Read);

  _hidInfoChar = _hidSvc.createCharacteristic(kHIDInfoUUID, BLEProperty::Read);
  _reportMapChar = _hidSvc.createCharacteristic(kReportMapUUID, BLEProperty::Read);
  _hidControlChar = _hidSvc.createCharacteristic(kHIDControlUUID, BLEProperty::WriteNR);
  _protocolModeChar = _hidSvc.createCharacteristic(kProtocolModeUUID, BLEProperty::Read | BLEProperty::WriteNR);

  uint8_t protocolMode = 1; // Report Protocol Mode
  _protocolModeChar.setValue(&protocolMode, 1);

  _batteryLevelChar = _batterySvc.createCharacteristic(kBatteryLevelUUID, BLEProperty::Read | BLEProperty::Notify);
}

void BLEHIDDevice::reportMap(const uint8_t *map, uint16_t size) {
  _reportMapChar.setValue(map, size);
}

void BLEHIDDevice::startServices() {
  _devInfoSvc.start();
  _hidSvc.start();
  _batterySvc.start();
}

BLEService BLEHIDDevice::deviceInfoService() { return _devInfoSvc; }
BLEService BLEHIDDevice::hidService() { return _hidSvc; }
BLEService BLEHIDDevice::batteryService() { return _batterySvc; }

BLECharacteristic BLEHIDDevice::manufacturer() { return _mfgChar; }

void BLEHIDDevice::manufacturer(const String &name) {
  _mfgChar.setValue(name);
}

void BLEHIDDevice::pnp(uint8_t sig, uint16_t vendorId, uint16_t productId, uint16_t version) {
  uint8_t pnpData[7];
  pnpData[0] = sig;
  pnpData[1] = vendorId & 0xFF;
  pnpData[2] = (vendorId >> 8) & 0xFF;
  pnpData[3] = productId & 0xFF;
  pnpData[4] = (productId >> 8) & 0xFF;
  pnpData[5] = version & 0xFF;
  pnpData[6] = (version >> 8) & 0xFF;
  _pnpChar.setValue(pnpData, sizeof(pnpData));
}

void BLEHIDDevice::hidInfo(uint8_t country, uint8_t flags) {
  uint8_t info[4];
  info[0] = 0x11; // bcdHID low byte (1.11)
  info[1] = 0x01; // bcdHID high byte
  info[2] = country;
  info[3] = flags;
  _hidInfoChar.setValue(info, sizeof(info));
}

void BLEHIDDevice::setBatteryLevel(uint8_t level) {
  _batteryLevelChar.setValue(&level, 1);
  _batteryLevelChar.notify();
}

BLECharacteristic BLEHIDDevice::hidControl() { return _hidControlChar; }
BLECharacteristic BLEHIDDevice::protocolMode() { return _protocolModeChar; }

static BLECharacteristic createReportChar(BLEService &svc, BLEProperty props, uint8_t reportId, uint8_t reportType) {
  BLECharacteristic chr = svc.createCharacteristic(kReportUUID, props);
  BLEDescriptor refDesc = chr.createDescriptor(kReportRefDescUUID, BLEPermission::Read);
  uint8_t refValue[2] = {reportId, reportType};
  refDesc.setValue(refValue, 2);
  return chr;
}

BLECharacteristic BLEHIDDevice::inputReport(uint8_t reportId) {
  return createReportChar(_hidSvc, BLEProperty::Read | BLEProperty::Notify, reportId, HID_REPORT_TYPE_INPUT);
}

BLECharacteristic BLEHIDDevice::outputReport(uint8_t reportId) {
  return createReportChar(_hidSvc, BLEProperty::Read | BLEProperty::Write | BLEProperty::WriteNR, reportId, HID_REPORT_TYPE_OUTPUT);
}

BLECharacteristic BLEHIDDevice::featureReport(uint8_t reportId) {
  return createReportChar(_hidSvc, BLEProperty::Read | BLEProperty::Write, reportId, HID_REPORT_TYPE_FEATURE);
}

BLECharacteristic BLEHIDDevice::bootInput() {
  return _hidSvc.createCharacteristic(kBootInputUUID, BLEProperty::Notify);
}

BLECharacteristic BLEHIDDevice::bootOutput() {
  return _hidSvc.createCharacteristic(kBootOutputUUID, BLEProperty::Read | BLEProperty::Write | BLEProperty::WriteNR);
}

#endif /* BLE_ENABLED */
