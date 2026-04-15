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

#include "impl/BLEGuards.h"
#if BLE_NIMBLE

#include "NimBLECharacteristic.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

// --------------------------------------------------------------------------
// BLEDescriptor public API
// --------------------------------------------------------------------------

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
  impl.onReadCb = handler;
  return BTStatus::OK;
}

BTStatus BLEDescriptor::onWrite(WriteHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onWriteCb = handler;
  return BTStatus::OK;
}

void BLEDescriptor::setValue(const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL();
  BLELockGuard lock(impl.mtx);
  impl.value.assign(data, data + length);
}

const uint8_t *BLEDescriptor::getValue(size_t *length) const {
  if (!_impl) {
    if (length) *length = 0;
    return nullptr;
  }
  BLELockGuard lock(_impl->mtx);
  if (length) *length = _impl->value.size();
  return _impl->value.empty() ? nullptr : _impl->value.data();
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

#endif /* BLE_NIMBLE */
