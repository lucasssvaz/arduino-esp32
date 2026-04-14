/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
 * Copyright 2017 Neil Kolban
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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)

#include "impl/BLEAdvertisedDeviceImpl.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

// --------------------------------------------------------------------------
// BLEAdvertisedDevice::Impl::parsePayload (shared payload parsing logic)
// --------------------------------------------------------------------------

void BLEAdvertisedDevice::Impl::parsePayload(const uint8_t *data, size_t len) {
  payload.assign(data, data + len);
  size_t pos = 0;
  while (pos < len) {
    uint8_t adLen = data[pos];
    if (adLen == 0 || pos + 1 + adLen > len) break;
    uint8_t adType = data[pos + 1];
    const uint8_t *adData = data + pos + 2;
    uint8_t dataLen = adLen - 1;

    switch (adType) {
      case 0x01: // Flags
        break;
      case 0x02: // Incomplete 16-bit UUIDs
      case 0x03: // Complete 16-bit UUIDs
        for (size_t i = 0; i + 1 < dataLen; i += 2) {
          uint16_t uuid16 = adData[i] | (adData[i + 1] << 8);
          serviceUUIDs.push_back(BLEUUID(uuid16));
        }
        break;
      case 0x04: // Incomplete 32-bit UUIDs
      case 0x05: // Complete 32-bit UUIDs
        for (size_t i = 0; i + 3 < dataLen; i += 4) {
          uint32_t uuid32 = adData[i] | (adData[i + 1] << 8) | (adData[i + 2] << 16) | (adData[i + 3] << 24);
          serviceUUIDs.push_back(BLEUUID(uuid32));
        }
        break;
      case 0x06: // Incomplete 128-bit UUIDs
      case 0x07: // Complete 128-bit UUIDs
        for (size_t i = 0; i + 15 < dataLen; i += 16) {
          serviceUUIDs.push_back(BLEUUID(adData + i, 16, true));
        }
        break;
      case 0x08: // Shortened Local Name
      case 0x09: // Complete Local Name
        name = String(reinterpret_cast<const char *>(adData), dataLen);
        hasName = true;
        break;
      case 0x0A: // TX Power Level
        if (dataLen >= 1) {
          txPower = static_cast<int8_t>(adData[0]);
          hasTXPower = true;
        }
        break;
      case 0x19: // Appearance
        if (dataLen >= 2) {
          appearance = adData[0] | (adData[1] << 8);
          hasAppearance = true;
        }
        break;
      case 0x14: // 16-bit Service Solicitation UUIDs
      case 0x15: // 128-bit Service Solicitation UUIDs
        break;
      case 0x16: // Service Data - 16-bit UUID
        if (dataLen >= 2) {
          ServiceDataEntry entry;
          entry.uuid = BLEUUID(static_cast<uint16_t>(adData[0] | (adData[1] << 8)));
          if (dataLen > 2) entry.data.assign(adData + 2, adData + dataLen);
          serviceData.push_back(std::move(entry));
        }
        break;
      case 0x20: // Service Data - 32-bit UUID
        if (dataLen >= 4) {
          ServiceDataEntry entry;
          entry.uuid = BLEUUID(static_cast<uint32_t>(adData[0] | (adData[1] << 8) | (adData[2] << 16) | (adData[3] << 24)));
          if (dataLen > 4) entry.data.assign(adData + 4, adData + dataLen);
          serviceData.push_back(std::move(entry));
        }
        break;
      case 0x21: // Service Data - 128-bit UUID
        if (dataLen >= 16) {
          ServiceDataEntry entry;
          entry.uuid = BLEUUID(adData, 16, true);
          if (dataLen > 16) entry.data.assign(adData + 16, adData + dataLen);
          serviceData.push_back(std::move(entry));
        }
        break;
      case 0xFF: // Manufacturer Specific Data
        mfgData.assign(adData, adData + dataLen);
        hasMfgData = true;
        break;
      default:
        break;
    }
    pos += 1 + adLen;
  }
}

// --------------------------------------------------------------------------
// BLEAdvertisedDevice public API
// --------------------------------------------------------------------------

BLEAdvertisedDevice::BLEAdvertisedDevice() : _impl(nullptr) {}
BLEAdvertisedDevice::operator bool() const { return _impl != nullptr; }

BTAddress BLEAdvertisedDevice::getAddress() const { return _impl ? _impl->address : BTAddress(); }
BTAddress::Type BLEAdvertisedDevice::getAddressType() const { return _impl ? _impl->addrType : BTAddress::Type::Public; }
String BLEAdvertisedDevice::getName() const { return _impl ? _impl->name : ""; }
int8_t BLEAdvertisedDevice::getRSSI() const { return _impl ? _impl->rssi : -128; }
int8_t BLEAdvertisedDevice::getTXPower() const { return _impl ? _impl->txPower : -128; }
uint16_t BLEAdvertisedDevice::getAppearance() const { return _impl ? _impl->appearance : 0; }
BLEAdvType BLEAdvertisedDevice::getAdvType() const { return _impl ? _impl->advType : BLEAdvType::ConnectableScannable; }

const uint8_t *BLEAdvertisedDevice::getManufacturerData(size_t *len) const {
  if (!_impl || _impl->mfgData.empty()) { if (len) *len = 0; return nullptr; }
  if (len) *len = _impl->mfgData.size();
  return _impl->mfgData.data();
}

String BLEAdvertisedDevice::getManufacturerDataString() const {
  BLE_CHECK_IMPL("");
  String s;
  for (uint8_t b : impl.mfgData) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02x", b);
    s += hex;
  }
  return s;
}

uint16_t BLEAdvertisedDevice::getManufacturerCompanyId() const {
  if (!_impl || _impl->mfgData.size() < 2) return 0;
  return _impl->mfgData[0] | (_impl->mfgData[1] << 8);
}

size_t BLEAdvertisedDevice::getServiceUUIDCount() const { return _impl ? _impl->serviceUUIDs.size() : 0; }
BLEUUID BLEAdvertisedDevice::getServiceUUID(size_t index) const {
  if (!_impl || index >= _impl->serviceUUIDs.size()) return BLEUUID();
  return _impl->serviceUUIDs[index];
}
bool BLEAdvertisedDevice::haveServiceUUID() const { return _impl && !_impl->serviceUUIDs.empty(); }
bool BLEAdvertisedDevice::isAdvertisingService(const BLEUUID &uuid) const {
  BLE_CHECK_IMPL(false);
  BLEUUID target = uuid.to128();
  for (const auto &u : impl.serviceUUIDs) {
    if (u.to128() == target) return true;
  }
  return false;
}

size_t BLEAdvertisedDevice::getServiceDataCount() const { return _impl ? _impl->serviceData.size() : 0; }
const uint8_t *BLEAdvertisedDevice::getServiceData(size_t index, size_t *len) const {
  if (!_impl || index >= _impl->serviceData.size()) { if (len) *len = 0; return nullptr; }
  if (len) *len = _impl->serviceData[index].data.size();
  return _impl->serviceData[index].data.data();
}
String BLEAdvertisedDevice::getServiceDataString(size_t index) const {
  if (!_impl || index >= _impl->serviceData.size()) return "";
  String s;
  for (uint8_t b : _impl->serviceData[index].data) {
    char hex[3]; snprintf(hex, sizeof(hex), "%02x", b); s += hex;
  }
  return s;
}
BLEUUID BLEAdvertisedDevice::getServiceDataUUID(size_t index) const {
  if (!_impl || index >= _impl->serviceData.size()) return BLEUUID();
  return _impl->serviceData[index].uuid;
}
bool BLEAdvertisedDevice::haveServiceData() const { return _impl && !_impl->serviceData.empty(); }

const uint8_t *BLEAdvertisedDevice::getPayload() const {
  if (!_impl || _impl->payload.empty()) return nullptr;
  return _impl->payload.data();
}
size_t BLEAdvertisedDevice::getPayloadLength() const { return _impl ? _impl->payload.size() : 0; }

bool BLEAdvertisedDevice::haveName() const { return _impl && _impl->hasName; }
bool BLEAdvertisedDevice::haveRSSI() const { return _impl && _impl->hasRSSI; }
bool BLEAdvertisedDevice::haveTXPower() const { return _impl && _impl->hasTXPower; }
bool BLEAdvertisedDevice::haveAppearance() const { return _impl && _impl->hasAppearance; }
bool BLEAdvertisedDevice::haveManufacturerData() const { return _impl && _impl->hasMfgData; }
bool BLEAdvertisedDevice::isConnectable() const { return _impl && _impl->connectable; }
bool BLEAdvertisedDevice::isScannable() const { return _impl && _impl->scannable; }
bool BLEAdvertisedDevice::isDirected() const { return _impl && _impl->directed; }
bool BLEAdvertisedDevice::isLegacyAdvertisement() const { return _impl ? _impl->legacy : true; }

BLEPhy BLEAdvertisedDevice::getPrimaryPhy() const { return _impl ? _impl->primaryPhy : BLEPhy::PHY_1M; }
BLEPhy BLEAdvertisedDevice::getSecondaryPhy() const { return _impl ? _impl->secondaryPhy : BLEPhy::PHY_1M; }
uint8_t BLEAdvertisedDevice::getAdvSID() const { return _impl ? _impl->sid : 0xFF; }
uint16_t BLEAdvertisedDevice::getPeriodicInterval() const { return _impl ? _impl->periodicInterval : 0; }

BLEAdvertisedDevice::FrameType BLEAdvertisedDevice::getFrameType() const { return Unknown; }

String BLEAdvertisedDevice::toString() const {
  BLE_CHECK_IMPL("BLEAdvertisedDevice(empty)");
  String s = "Name: " + impl.name + ", Addr: " + impl.address.toString();
  s += ", RSSI: " + String(impl.rssi);
  return s;
}

// --------------------------------------------------------------------------
// BLEScanResults
// --------------------------------------------------------------------------

void BLEScanResults::appendOrReplace(const BLEAdvertisedDevice &device) {
  for (auto &existing : _devices) {
    if (existing.getAddress() == device.getAddress()) {
      existing = device;
      return;
    }
  }
  _devices.push_back(device);
}

size_t BLEScanResults::getCount() const { return _devices.size(); }
BLEAdvertisedDevice BLEScanResults::getDevice(size_t index) const {
  return (index < _devices.size()) ? _devices[index] : BLEAdvertisedDevice();
}
void BLEScanResults::dump() const {
  for (size_t i = 0; i < _devices.size(); i++) {
    log_i("[%d] %s", i, _devices[i].toString().c_str());
  }
}
const BLEAdvertisedDevice *BLEScanResults::begin() const { return _devices.data(); }
const BLEAdvertisedDevice *BLEScanResults::end() const { return _devices.data() + _devices.size(); }

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
