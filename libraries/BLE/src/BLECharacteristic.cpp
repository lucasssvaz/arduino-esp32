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
#include "impl/BLEMutex.h"

// --------------------------------------------------------------------------
// BLECharacteristic common API (stack-agnostic)
// --------------------------------------------------------------------------

BLECharacteristic::BLECharacteristic() : _impl(nullptr) {}
BLECharacteristic::operator bool() const { return _impl != nullptr; }

void BLECharacteristic::setValue(const String &value) {
  setValue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
}
void BLECharacteristic::setValue(uint16_t v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }
void BLECharacteristic::setValue(uint32_t v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }
void BLECharacteristic::setValue(float v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }
void BLECharacteristic::setValue(double v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }

BLEProperty BLECharacteristic::getProperties() const {
  return _impl ? _impl->properties : BLEProperty{};
}

BLEPermission BLECharacteristic::getPermissions() const {
  return _impl ? _impl->permissions : BLEPermission{};
}

void BLECharacteristic::setPermissions(BLEPermission permissions) {
  BLE_CHECK_IMPL();
  impl.permissions = permissions;
}

BLEDescriptor BLECharacteristic::getDescriptor(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLEDescriptor());
  for (auto &d : impl.descriptors) {
    if (d->uuid == uuid) {
      return BLEDescriptor(d);
    }
  }
  return BLEDescriptor();
}

std::vector<BLEDescriptor> BLECharacteristic::getDescriptors() const {
  std::vector<BLEDescriptor> result;
  BLE_CHECK_IMPL(result);
  result.reserve(impl.descriptors.size());
  for (auto &d : impl.descriptors) {
    result.push_back(BLEDescriptor(d));
  }
  return result;
}

void BLECharacteristic::removeDescriptor(const BLEDescriptor &desc) {
  if (!_impl || !desc._impl) return;
  auto &descs = _impl->descriptors;
  for (auto it = descs.begin(); it != descs.end(); ++it) {
    if (*it == desc._impl) {
      descs.erase(it);
      break;
    }
  }
}

BLEUUID BLECharacteristic::getUUID() const {
  return _impl ? _impl->uuid : BLEUUID();
}

uint16_t BLECharacteristic::getHandle() const {
  return _impl ? _impl->handle : 0;
}

BLEService BLECharacteristic::getService() const {
  return _impl && _impl->service ? BLEService(std::shared_ptr<BLEService::Impl>(_impl->service, [](BLEService::Impl *){})) : BLEService();
}

String BLECharacteristic::toString() const {
  BLE_CHECK_IMPL("BLECharacteristic(null)");
  return "BLECharacteristic(uuid=" + impl.uuid.toString() + ")";
}

void BLECharacteristic::setValue(int value) { setValue(static_cast<uint32_t>(value)); }

BTStatus BLECharacteristic::onRead(ReadHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onReadCb = handler;
  return BTStatus::OK;
}

BTStatus BLECharacteristic::onWrite(WriteHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onWriteCb = handler;
  return BTStatus::OK;
}

BTStatus BLECharacteristic::onNotify(NotifyHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onNotifyCb = handler;
  return BTStatus::OK;
}

BTStatus BLECharacteristic::onSubscribe(SubscribeHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onSubscribeCb = handler;
  return BTStatus::OK;
}

BTStatus BLECharacteristic::onStatus(StatusHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onStatusCb = handler;
  return BTStatus::OK;
}

size_t BLECharacteristic::getSubscribedCount() const {
  return _impl ? _impl->subscribers.size() : 0;
}

bool BLECharacteristic::isSubscribed(uint16_t connHandle) const {
  BLE_CHECK_IMPL(false);
  for (const auto &sub : impl.subscribers) {
    if (sub.first == connHandle) return sub.second > 0;
  }
  return false;
}

std::vector<uint16_t> BLECharacteristic::getSubscribedConnections() const {
  std::vector<uint16_t> result;
  BLE_CHECK_IMPL(result);
  for (const auto &sub : impl.subscribers) {
    if (sub.second > 0) result.push_back(sub.first);
  }
  return result;
}

void BLECharacteristic::setValue(const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL();
  BLELockGuard lock(impl.valueMtx);
  impl.value.assign(data, data + length);
}

const uint8_t *BLECharacteristic::getValue(size_t *length) const {
  if (!_impl) {
    if (length) *length = 0;
    return nullptr;
  }
  BLELockGuard lock(_impl->valueMtx);
  if (length) *length = _impl->value.size();
  return _impl->value.empty() ? nullptr : _impl->value.data();
}

String BLECharacteristic::getStringValue() const {
  BLE_CHECK_IMPL("");
  BLELockGuard lock(impl.valueMtx);
  return String(reinterpret_cast<const char *>(impl.value.data()), impl.value.size());
}

#endif /* BLE_ENABLED */
