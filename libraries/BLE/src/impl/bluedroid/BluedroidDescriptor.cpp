/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
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
#if defined(SOC_BLE_SUPPORTED) && defined(CONFIG_BLUEDROID_ENABLED)

#include "BluedroidDescriptor.h"
#include "impl/BLEImplHelpers.h"

// --------------------------------------------------------------------------
// BLEDescriptor -- Bluedroid
// --------------------------------------------------------------------------

BLEDescriptor::BLEDescriptor() : _impl(nullptr) {}
BLEDescriptor::operator bool() const { return _impl != nullptr; }

BLEDescriptor::BLEDescriptor(const BLEUUID &uuid, uint16_t) : _impl(nullptr) {
  _impl = std::make_shared<BLEDescriptor::Impl>();
  _impl->uuid = uuid;
}

void BLEDescriptor::setValue(const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL(); impl.value.assign(data, data + length);
}
void BLEDescriptor::setValue(const String &value) { setValue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length()); }

const uint8_t *BLEDescriptor::getValue(size_t *length) const {
  if (!_impl || _impl->value.empty()) { if (length) *length = 0; return nullptr; }
  if (length) *length = _impl->value.size();
  return _impl->value.data();
}

size_t BLEDescriptor::getLength() const { return _impl ? _impl->value.size() : 0; }
void BLEDescriptor::setPermissions(BLEPermission) {}

BTStatus BLEDescriptor::onRead(ReadHandler) { return BTStatus::NotSupported; }
BTStatus BLEDescriptor::onWrite(WriteHandler) { return BTStatus::NotSupported; }

BLEUUID BLEDescriptor::getUUID() const { return _impl ? _impl->uuid : BLEUUID(); }
uint16_t BLEDescriptor::getHandle() const { return _impl ? _impl->handle : 0; }

BLECharacteristic BLEDescriptor::getCharacteristic() const {
  return _impl ? BLECharacteristic(_impl->charImpl.lock()) : BLECharacteristic();
}

String BLEDescriptor::toString() const { return _impl ? "BLEDescriptor(uuid=" + _impl->uuid.toString() + ")" : "BLEDescriptor(null)"; }

BLEDescriptor BLEDescriptor::createUserDescription(const String &text) {
  auto impl = std::make_shared<BLEDescriptor::Impl>();
  impl->uuid = BLEUUID(static_cast<uint16_t>(0x2901));
  impl->value.assign(text.c_str(), text.c_str() + text.length());
  return BLEDescriptor(impl);
}

BLEDescriptor BLEDescriptor::createPresentationFormat() {
  auto impl = std::make_shared<BLEDescriptor::Impl>();
  impl->uuid = BLEUUID(static_cast<uint16_t>(0x2904));
  impl->value.resize(7, 0);
  return BLEDescriptor(impl);
}

BLEDescriptor BLEDescriptor::createCCCD() {
  auto impl = std::make_shared<BLEDescriptor::Impl>();
  impl->uuid = BLEUUID(static_cast<uint16_t>(0x2902));
  impl->value.resize(2, 0);
  return BLEDescriptor(impl);
}

bool BLEDescriptor::isUserDescription() const { return _impl && _impl->uuid == BLEUUID(static_cast<uint16_t>(0x2901)); }
bool BLEDescriptor::isCCCD() const { return _impl && _impl->uuid == BLEUUID(static_cast<uint16_t>(0x2902)); }
bool BLEDescriptor::isPresentationFormat() const { return _impl && _impl->uuid == BLEUUID(static_cast<uint16_t>(0x2904)); }

void BLEDescriptor::setUserDescription(const String &description) { if (isUserDescription()) setValue(description); }
String BLEDescriptor::getUserDescription() const {
  if (!isUserDescription() || !_impl) return "";
  return String(reinterpret_cast<const char *>(_impl->value.data()), _impl->value.size());
}

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

void BLEDescriptor::setFormat(uint8_t format) { if (isPresentationFormat() && _impl && _impl->value.size() >= 7) _impl->value[0] = format; }
void BLEDescriptor::setExponent(int8_t exponent) { if (isPresentationFormat() && _impl && _impl->value.size() >= 7) _impl->value[1] = static_cast<uint8_t>(exponent); }
void BLEDescriptor::setUnit(uint16_t unit) { if (isPresentationFormat() && _impl && _impl->value.size() >= 7) { _impl->value[2] = unit & 0xFF; _impl->value[3] = unit >> 8; } }
void BLEDescriptor::setNamespace(uint8_t ns) { if (isPresentationFormat() && _impl && _impl->value.size() >= 7) _impl->value[4] = ns; }
void BLEDescriptor::setFormatDescription(uint16_t description) { if (isPresentationFormat() && _impl && _impl->value.size() >= 7) { _impl->value[5] = description & 0xFF; _impl->value[6] = description >> 8; } }

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
