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

#include "NimBLECharacteristic.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

// --------------------------------------------------------------------------
// BLEDescriptor public API
// --------------------------------------------------------------------------

BLEDescriptor::BLEDescriptor() : _impl(nullptr) {}
BLEDescriptor::operator bool() const { return _impl != nullptr; }

BLEDescriptor::BLEDescriptor(const BLEUUID &uuid, uint16_t maxLength) : _impl(nullptr) {
  auto impl = std::make_shared<BLEDescriptor::Impl>();
  impl->uuid = uuid;
  uuidToNimble(uuid, impl->nimbleUUID);
  impl->attFlags = BLE_ATT_F_READ;
  impl->value.reserve(maxLength);
  _impl = impl;
}

BTStatus BLEDescriptor::onRead(ReadHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onReadCb = std::move(handler);
  return BTStatus::OK;
}

BTStatus BLEDescriptor::onWrite(WriteHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onWriteCb = std::move(handler);
  return BTStatus::OK;
}

void BLEDescriptor::setValue(const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL();
  std::lock_guard<std::mutex> lock(impl.mtx);
  impl.value.assign(data, data + length);
}

void BLEDescriptor::setValue(const String &value) {
  setValue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
}

const uint8_t *BLEDescriptor::getValue(size_t *length) const {
  if (!_impl) {
    if (length) *length = 0;
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_impl->mtx);
  if (length) *length = _impl->value.size();
  return _impl->value.empty() ? nullptr : _impl->value.data();
}

size_t BLEDescriptor::getLength() const {
  return _impl ? _impl->value.size() : 0;
}

void BLEDescriptor::setPermissions(BLEPermission perms) {
  BLE_CHECK_IMPL();
  uint8_t flags = 0;
  uint16_t p = static_cast<uint16_t>(perms);
  if (p & 0x000F) flags |= BLE_ATT_F_READ;
  if (p & 0x00F0) flags |= BLE_ATT_F_WRITE;
  if (p & static_cast<uint16_t>(BLEPermission::ReadEncrypted)) flags |= BLE_ATT_F_READ_ENC;
  if (p & static_cast<uint16_t>(BLEPermission::ReadAuthenticated)) flags |= BLE_ATT_F_READ_AUTHEN;
  if (p & static_cast<uint16_t>(BLEPermission::ReadAuthorized)) flags |= BLE_ATT_F_READ_AUTHOR;
  if (p & static_cast<uint16_t>(BLEPermission::WriteEncrypted)) flags |= BLE_ATT_F_WRITE_ENC;
  if (p & static_cast<uint16_t>(BLEPermission::WriteAuthenticated)) flags |= BLE_ATT_F_WRITE_AUTHEN;
  if (p & static_cast<uint16_t>(BLEPermission::WriteAuthorized)) flags |= BLE_ATT_F_WRITE_AUTHOR;
  impl.attFlags = flags;
}

BLEUUID BLEDescriptor::getUUID() const {
  return _impl ? _impl->uuid : BLEUUID();
}

uint16_t BLEDescriptor::getHandle() const {
  return _impl ? _impl->handle : 0;
}

BLECharacteristic BLEDescriptor::getCharacteristic() const {
  return _impl ? BLECharacteristic(_impl->charImpl.lock()) : BLECharacteristic();
}

String BLEDescriptor::toString() const {
  BLE_CHECK_IMPL("BLEDescriptor(null)");
  return "BLEDescriptor(uuid=" + impl.uuid.toString() + ")";
}

BLEDescriptor BLEDescriptor::createUserDescription(const String &text) {
  auto impl = std::make_shared<BLEDescriptor::Impl>();
  impl->uuid = BLEUUID(static_cast<uint16_t>(0x2901));
  uuidToNimble(impl->uuid, impl->nimbleUUID);
  impl->value.assign(text.c_str(), text.c_str() + text.length());
  impl->attFlags = BLE_ATT_F_READ;
  return BLEDescriptor(impl);
}

BLEDescriptor BLEDescriptor::createPresentationFormat() {
  auto impl = std::make_shared<BLEDescriptor::Impl>();
  impl->uuid = BLEUUID(static_cast<uint16_t>(0x2904));
  uuidToNimble(impl->uuid, impl->nimbleUUID);
  impl->value.resize(7, 0);
  impl->attFlags = BLE_ATT_F_READ;
  return BLEDescriptor(impl);
}

BLEDescriptor BLEDescriptor::createCCCD() {
  auto impl = std::make_shared<BLEDescriptor::Impl>();
  impl->uuid = BLEUUID(static_cast<uint16_t>(0x2902));
  uuidToNimble(impl->uuid, impl->nimbleUUID);
  impl->value.resize(2, 0);
  impl->attFlags = BLE_ATT_F_READ | BLE_ATT_F_WRITE;
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

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
