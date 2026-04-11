/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
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
#if (defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)) && defined(CONFIG_NIMBLE_ENABLED)

#include "BLEAdvertisementData.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

// --------------------------------------------------------------------------
// BLEAdvertisementData implementation
// --------------------------------------------------------------------------

void BLEAdvertisementData::addField(uint8_t type, const uint8_t *data, size_t len) {
  if (len + 2 + _payload.size() > 31) {
    log_w("Ad field would exceed 31 bytes, skipping type=0x%02x", type);
    return;
  }
  _payload.push_back(static_cast<uint8_t>(len + 1));
  _payload.push_back(type);
  _payload.insert(_payload.end(), data, data + len);
}

void BLEAdvertisementData::setFlags(uint8_t flags) {
  addField(0x01, &flags, 1);
}

void BLEAdvertisementData::setCompleteServices(const BLEUUID &uuid) {
  uint8_t type;
  uint8_t buf[16];
  size_t len;
  switch (uuid.bitSize()) {
    case 16:
      type = 0x03;
      buf[0] = uuid.data()[3]; buf[1] = uuid.data()[2];
      len = 2;
      break;
    case 32:
      type = 0x05;
      buf[0] = uuid.data()[3]; buf[1] = uuid.data()[2]; buf[2] = uuid.data()[1]; buf[3] = uuid.data()[0];
      len = 4;
      break;
    default: {
      type = 0x07;
      BLEUUID u128 = uuid.to128();
      const uint8_t *be = u128.data();
      for (int i = 0; i < 16; i++) buf[15 - i] = be[i];
      len = 16;
      break;
    }
  }
  addField(type, buf, len);
}

void BLEAdvertisementData::setPartialServices(const BLEUUID &uuid) {
  uint8_t type;
  uint8_t buf[16];
  size_t len;
  switch (uuid.bitSize()) {
    case 16:
      type = 0x02; buf[0] = uuid.data()[3]; buf[1] = uuid.data()[2]; len = 2; break;
    case 32:
      type = 0x04; buf[0] = uuid.data()[3]; buf[1] = uuid.data()[2]; buf[2] = uuid.data()[1]; buf[3] = uuid.data()[0]; len = 4; break;
    default: {
      type = 0x06;
      BLEUUID u128 = uuid.to128();
      const uint8_t *be = u128.data();
      for (int i = 0; i < 16; i++) buf[15 - i] = be[i];
      len = 16;
      break;
    }
  }
  addField(type, buf, len);
}

void BLEAdvertisementData::addServiceUUID(const BLEUUID &uuid) {
  setCompleteServices(uuid);
}

void BLEAdvertisementData::setServiceData(const BLEUUID &uuid, const uint8_t *data, size_t len) {
  std::vector<uint8_t> buf;
  if (uuid.bitSize() == 16) {
    buf.push_back(uuid.data()[3]); buf.push_back(uuid.data()[2]);
  } else {
    BLEUUID u128 = uuid.to128();
    const uint8_t *be = u128.data();
    for (int i = 15; i >= 0; i--) buf.push_back(be[i]);
  }
  buf.insert(buf.end(), data, data + len);
  uint8_t type = (uuid.bitSize() == 16) ? 0x16 : 0x21;
  addField(type, buf.data(), buf.size());
}

void BLEAdvertisementData::setManufacturerData(uint16_t companyId, const uint8_t *data, size_t len) {
  std::vector<uint8_t> buf;
  buf.push_back(companyId & 0xFF);
  buf.push_back((companyId >> 8) & 0xFF);
  buf.insert(buf.end(), data, data + len);
  addField(0xFF, buf.data(), buf.size());
}

void BLEAdvertisementData::setName(const String &name, bool complete) {
  addField(complete ? 0x09 : 0x08, reinterpret_cast<const uint8_t *>(name.c_str()), name.length());
}

void BLEAdvertisementData::setShortName(const String &name) {
  setName(name, false);
}

void BLEAdvertisementData::setAppearance(uint16_t appearance) {
  uint8_t buf[2] = {static_cast<uint8_t>(appearance & 0xFF), static_cast<uint8_t>(appearance >> 8)};
  addField(0x19, buf, 2);
}

void BLEAdvertisementData::setPreferredParams(uint16_t minInterval, uint16_t maxInterval) {
  uint8_t buf[4] = {
    static_cast<uint8_t>(minInterval & 0xFF), static_cast<uint8_t>(minInterval >> 8),
    static_cast<uint8_t>(maxInterval & 0xFF), static_cast<uint8_t>(maxInterval >> 8),
  };
  addField(0x12, buf, 4);
}

void BLEAdvertisementData::setTxPower(int8_t txPower) {
  addField(0x0A, reinterpret_cast<uint8_t *>(&txPower), 1);
}

void BLEAdvertisementData::addRaw(const uint8_t *data, size_t len) {
  _payload.insert(_payload.end(), data, data + len);
}

void BLEAdvertisementData::clear() {
  _payload.clear();
}

const uint8_t *BLEAdvertisementData::data() const {
  return _payload.data();
}

size_t BLEAdvertisementData::length() const {
  return _payload.size();
}

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
