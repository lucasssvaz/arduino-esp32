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

#include "impl/BLECharacteristicBackend.h"
#include "impl/BLEImplHelpers.h"

// --------------------------------------------------------------------------
// BLEDescriptor common API (stack-agnostic)
// --------------------------------------------------------------------------

BLEDescriptor::BLEDescriptor() : _impl(nullptr) {}
BLEDescriptor::operator bool() const { return _impl != nullptr; }

BLEUUID BLEDescriptor::getUUID() const { return _impl ? _impl->uuid : BLEUUID(); }
uint16_t BLEDescriptor::getHandle() const { return _impl ? _impl->handle : 0; }

BLECharacteristic BLEDescriptor::getCharacteristic() const {
  return _impl && _impl->chr ? BLECharacteristic(std::shared_ptr<BLECharacteristic::Impl>(_impl->chr, [](BLECharacteristic::Impl *){})) : BLECharacteristic();
}

String BLEDescriptor::toString() const {
  BLE_CHECK_IMPL("BLEDescriptor(null)");
  return "BLEDescriptor(uuid=" + impl.uuid.toString() + ")";
}

void BLEDescriptor::setValue(const String &value) {
  setValue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
}

size_t BLEDescriptor::getLength() const {
  return _impl ? _impl->value.size() : 0;
}

// Type queries
bool BLEDescriptor::isUserDescription() const { return _impl && _impl->uuid == BLEUUID(static_cast<uint16_t>(0x2901)); }
bool BLEDescriptor::isCCCD() const { return _impl && _impl->uuid == BLEUUID(static_cast<uint16_t>(0x2902)); }
bool BLEDescriptor::isPresentationFormat() const { return _impl && _impl->uuid == BLEUUID(static_cast<uint16_t>(0x2904)); }

// 0x2901 User Description convenience
void BLEDescriptor::setUserDescription(const String &description) { if (isUserDescription()) setValue(description); }
String BLEDescriptor::getUserDescription() const {
  if (!isUserDescription() || !_impl) return "";
  return String(reinterpret_cast<const char *>(_impl->value.data()), _impl->value.size());
}

// 0x2902 CCCD convenience
bool BLEDescriptor::getNotifications() const { return isCCCD() && _impl && _impl->value.size() >= 2 && (_impl->value[0] & 0x01); }
bool BLEDescriptor::getIndications() const { return isCCCD() && _impl && _impl->value.size() >= 2 && (_impl->value[0] & 0x02); }
void BLEDescriptor::setNotifications(bool enable) {
  if (!isCCCD() || !_impl) return;
  if (_impl->value.size() < 2) _impl->value.resize(2, 0);
  if (enable) _impl->value[0] |= 0x01; else _impl->value[0] &= ~0x01;
}
void BLEDescriptor::setIndications(bool enable) {
  if (!isCCCD() || !_impl) return;
  if (_impl->value.size() < 2) _impl->value.resize(2, 0);
  if (enable) _impl->value[0] |= 0x02; else _impl->value[0] &= ~0x02;
}

// 0x2904 Presentation Format convenience
void BLEDescriptor::setFormat(uint8_t format) { if (isPresentationFormat() && _impl && _impl->value.size() >= 7) _impl->value[0] = format; }
void BLEDescriptor::setExponent(int8_t exponent) { if (isPresentationFormat() && _impl && _impl->value.size() >= 7) _impl->value[1] = static_cast<uint8_t>(exponent); }
void BLEDescriptor::setUnit(uint16_t unit) { if (isPresentationFormat() && _impl && _impl->value.size() >= 7) { _impl->value[2] = unit & 0xFF; _impl->value[3] = unit >> 8; } }
void BLEDescriptor::setNamespace(uint8_t ns) { if (isPresentationFormat() && _impl && _impl->value.size() >= 7) _impl->value[4] = ns; }
void BLEDescriptor::setFormatDescription(uint16_t description) { if (isPresentationFormat() && _impl && _impl->value.size() >= 7) { _impl->value[5] = description & 0xFF; _impl->value[6] = description >> 8; } }

#endif /* BLE_ENABLED */
