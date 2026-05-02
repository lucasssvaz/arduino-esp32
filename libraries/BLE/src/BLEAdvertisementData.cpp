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

#include "impl/BLEGuards.h"
#if BLE_ENABLED

#include "BLEAdvertisementData.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

// --------------------------------------------------------------------------
// BLEAdvertisementData implementation (stack-agnostic)
// --------------------------------------------------------------------------

/**
 * @brief Append an AD structure (length + type + value) to the payload.
 * @param type The AD type code (e.g. 0x01 for Flags, 0xFF for Manufacturer Data).
 * @param data Pointer to the AD value bytes.
 * @param len Length of @p data in bytes.
 * @note Silently skips the field if it would cause the total payload to exceed
 *       the 31-byte legacy advertising limit.
 */
void BLEAdvertisementData::addField(uint8_t type, const uint8_t *data, size_t len) {
  if (len + 2 + _payload.size() > 31) {
    log_w("Ad field would exceed 31 bytes, skipping type=0x%02x", type);
    return;
  }
  _payload.push_back(static_cast<uint8_t>(len + 1));
  _payload.push_back(type);
  _payload.insert(_payload.end(), data, data + len);
}

/**
 * @brief Set the AD Flags field (AD type 0x01).
 * @param flags Bitmask of AD flag values.
 */
void BLEAdvertisementData::setFlags(uint8_t flags) {
  addField(0x01, &flags, 1);
}

/**
 * @brief Add a Complete List of Service UUIDs AD field.
 * @param uuid The service UUID to advertise.
 * @note UUID bytes are converted from BLEUUID's big-endian storage to the
 *       little-endian wire format required by the BLE spec.
 */
void BLEAdvertisementData::setCompleteServices(const BLEUUID &uuid) {
  uint8_t type;
  uint8_t buf[16];
  size_t len;
  switch (uuid.bitSize()) {
    case 16:
      type = 0x03;
      buf[0] = uuid.data()[3];
      buf[1] = uuid.data()[2];
      len = 2;
      break;
    case 32:
      type = 0x05;
      buf[0] = uuid.data()[3];
      buf[1] = uuid.data()[2];
      buf[2] = uuid.data()[1];
      buf[3] = uuid.data()[0];
      len = 4;
      break;
    default:
    {
      type = 0x07;
      BLEUUID u128 = uuid.to128();
      const uint8_t *be = u128.data();
      for (int i = 0; i < 16; i++) {
        buf[15 - i] = be[i];
      }
      len = 16;
      break;
    }
  }
  addField(type, buf, len);
}

/**
 * @brief Add an Incomplete List of Service UUIDs AD field.
 * @param uuid The service UUID to advertise.
 * @note UUID bytes are converted from big-endian to little-endian wire format.
 */
void BLEAdvertisementData::setPartialServices(const BLEUUID &uuid) {
  uint8_t type;
  uint8_t buf[16];
  size_t len;
  switch (uuid.bitSize()) {
    case 16:
      type = 0x02;
      buf[0] = uuid.data()[3];
      buf[1] = uuid.data()[2];
      len = 2;
      break;
    case 32:
      type = 0x04;
      buf[0] = uuid.data()[3];
      buf[1] = uuid.data()[2];
      buf[2] = uuid.data()[1];
      buf[3] = uuid.data()[0];
      len = 4;
      break;
    default:
    {
      type = 0x06;
      BLEUUID u128 = uuid.to128();
      const uint8_t *be = u128.data();
      for (int i = 0; i < 16; i++) {
        buf[15 - i] = be[i];
      }
      len = 16;
      break;
    }
  }
  addField(type, buf, len);
}

/**
 * @brief Append a service UUID via a Complete List AD field.
 * @param uuid The service UUID to add.
 */
void BLEAdvertisementData::addServiceUUID(const BLEUUID &uuid) {
  setCompleteServices(uuid);
}

/**
 * @brief Set a Service Data AD field.
 * @param uuid The service UUID this data belongs to (16-bit or 128-bit).
 * @param data Pointer to the service data payload.
 * @param len Length of @p data in bytes.
 * @note Uses AD type 0x16 for 16-bit UUIDs and 0x21 for 128-bit UUIDs.
 *       UUID bytes are stored in little-endian wire format.
 */
void BLEAdvertisementData::setServiceData(const BLEUUID &uuid, const uint8_t *data, size_t len) {
  std::vector<uint8_t> buf;
  if (uuid.bitSize() == 16) {
    buf.push_back(uuid.data()[3]);
    buf.push_back(uuid.data()[2]);
  } else {
    BLEUUID u128 = uuid.to128();
    const uint8_t *be = u128.data();
    for (int i = 15; i >= 0; i--) {
      buf.push_back(be[i]);
    }
  }
  buf.insert(buf.end(), data, data + len);
  uint8_t type = (uuid.bitSize() == 16) ? 0x16 : 0x21;
  addField(type, buf.data(), buf.size());
}

/**
 * @brief Set a Manufacturer Specific Data AD field (type 0xFF).
 * @param companyId Bluetooth SIG company identifier (stored little-endian).
 * @param data Pointer to the manufacturer-specific payload (after the company ID).
 * @param len Length of @p data in bytes.
 */
void BLEAdvertisementData::setManufacturerData(uint16_t companyId, const uint8_t *data, size_t len) {
  std::vector<uint8_t> buf;
  buf.push_back(companyId & 0xFF);
  buf.push_back((companyId >> 8) & 0xFF);
  buf.insert(buf.end(), data, data + len);
  addField(0xFF, buf.data(), buf.size());
}

/**
 * @brief Set the local name AD field.
 * @param name The device name string.
 * @param complete If true, emits Complete Local Name (0x09); if false, Shortened (0x08).
 */
void BLEAdvertisementData::setName(const String &name, bool complete) {
  addField(complete ? 0x09 : 0x08, reinterpret_cast<const uint8_t *>(name.c_str()), name.length());
}

/**
 * @brief Set a Shortened Local Name AD field.
 * @param name The shortened device name string.
 */
void BLEAdvertisementData::setShortName(const String &name) {
  setName(name, false);
}

/**
 * @brief Set the Appearance AD field (type 0x19).
 * @param appearance GAP appearance value stored little-endian.
 */
void BLEAdvertisementData::setAppearance(uint16_t appearance) {
  uint8_t buf[2] = {static_cast<uint8_t>(appearance & 0xFF), static_cast<uint8_t>(appearance >> 8)};
  addField(0x19, buf, 2);
}

/**
 * @brief Set the Peripheral Preferred Connection Parameters AD field (type 0x12).
 * @param minInterval Minimum connection interval in units of 1.25 ms.
 * @param maxInterval Maximum connection interval in units of 1.25 ms.
 */
void BLEAdvertisementData::setPreferredParams(uint16_t minInterval, uint16_t maxInterval) {
  uint8_t buf[4] = {
    static_cast<uint8_t>(minInterval & 0xFF),
    static_cast<uint8_t>(minInterval >> 8),
    static_cast<uint8_t>(maxInterval & 0xFF),
    static_cast<uint8_t>(maxInterval >> 8),
  };
  addField(0x12, buf, 4);
}

/**
 * @brief Set the TX Power Level AD field (type 0x0A).
 * @param txPower Transmit power in dBm.
 */
void BLEAdvertisementData::setTxPower(int8_t txPower) {
  addField(0x0A, reinterpret_cast<uint8_t *>(&txPower), 1);
}

/**
 * @brief Append raw bytes directly to the payload without wrapping in an AD structure.
 * @param data Pointer to a pre-formed AD structure (length + type + value).
 * @param len Total length of the data in bytes.
 * @note Does not enforce the 31-byte limit; caller is responsible for validity.
 */
void BLEAdvertisementData::addRaw(const uint8_t *data, size_t len) {
  _payload.insert(_payload.end(), data, data + len);
}

/**
 * @brief Clear all previously set AD structures, resetting the payload to empty.
 */
void BLEAdvertisementData::clear() {
  _payload.clear();
}

/**
 * @brief Get a pointer to the assembled advertisement payload.
 * @return Pointer to the raw payload bytes.
 */
const uint8_t *BLEAdvertisementData::data() const {
  return _payload.data();
}

/**
 * @brief Get the total length of the assembled advertisement payload.
 * @return Length in bytes.
 */
size_t BLEAdvertisementData::length() const {
  return _payload.size();
}

#endif /* BLE_ENABLED */
