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

#include "impl/BLERemoteTypesBackend.h"
#include "impl/BLEImplHelpers.h"

#include <string.h>

// --------------------------------------------------------------------------
// BLERemoteCharacteristic common API (stack-agnostic)
// --------------------------------------------------------------------------

BLERemoteCharacteristic::BLERemoteCharacteristic() : _impl(nullptr) {}
BLERemoteCharacteristic::operator bool() const { return _impl != nullptr; }

BLEUUID BLERemoteCharacteristic::getUUID() const {
  return _impl ? _impl->uuid : BLEUUID();
}

uint8_t BLERemoteCharacteristic::readUInt8(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  return v.length() >= 1 ? static_cast<uint8_t>(v[0]) : 0;
}

uint16_t BLERemoteCharacteristic::readUInt16(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  if (v.length() < 2) return 0;
  return static_cast<uint16_t>(static_cast<uint8_t>(v[0])) |
         (static_cast<uint16_t>(static_cast<uint8_t>(v[1])) << 8);
}

uint32_t BLERemoteCharacteristic::readUInt32(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  if (v.length() < 4) return 0;
  return static_cast<uint32_t>(static_cast<uint8_t>(v[0])) |
         (static_cast<uint32_t>(static_cast<uint8_t>(v[1])) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(v[2])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(v[3])) << 24);
}

float BLERemoteCharacteristic::readFloat(uint32_t timeoutMs) {
  uint32_t raw = readUInt32(timeoutMs);
  float f;
  memcpy(&f, &raw, sizeof(f));
  return f;
}

size_t BLERemoteCharacteristic::readValue(uint8_t *buf, size_t bufLen, uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  size_t copyLen = (v.length() < bufLen) ? v.length() : bufLen;
  memcpy(buf, v.c_str(), copyLen);
  return copyLen;
}

BTStatus BLERemoteCharacteristic::writeValue(const String &value, bool withResponse) {
  return writeValue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length(), withResponse);
}

BTStatus BLERemoteCharacteristic::writeValue(uint8_t value, bool withResponse) {
  return writeValue(&value, 1, withResponse);
}

BLERemoteService BLERemoteCharacteristic::getRemoteService() const {
  return (_impl && _impl->service)
    ? BLERemoteService(std::shared_ptr<BLERemoteService::Impl>(_impl->service, [](BLERemoteService::Impl *){}))
    : BLERemoteService();
}

String BLERemoteCharacteristic::toString() const {
  BLE_CHECK_IMPL("BLERemoteCharacteristic(empty)");
  return "BLERemoteCharacteristic(uuid=" + impl.uuid.toString() + ")";
}

#endif /* BLE_ENABLED */
