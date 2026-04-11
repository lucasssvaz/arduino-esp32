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

#include "BLE.h"

#include "BluedroidDescriptor.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <esp_gatts_api.h>
#include <algorithm>
#include <vector>

struct BLECharacteristic::Impl {
  BLEUUID uuid;
  uint16_t handle = 0;
  BLEProperty properties = static_cast<BLEProperty>(0);
  BLEPermission permissions = static_cast<BLEPermission>(0);
  std::vector<uint8_t> value;
  std::vector<std::shared_ptr<BLEDescriptor::Impl>> descriptors;
  std::weak_ptr<BLEService::Impl> serviceImpl;
};

// --------------------------------------------------------------------------
// BLECharacteristic -- Bluedroid
// --------------------------------------------------------------------------

BLECharacteristic::BLECharacteristic() : _impl(nullptr) {}
BLECharacteristic::operator bool() const { return _impl != nullptr; }

void BLECharacteristic::setValue(const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL(); impl.value.assign(data, data + length);
}
void BLECharacteristic::setValue(const String &value) { setValue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length()); }
void BLECharacteristic::setValue(uint16_t value) { setValue(reinterpret_cast<const uint8_t *>(&value), sizeof(value)); }
void BLECharacteristic::setValue(uint32_t value) { setValue(reinterpret_cast<const uint8_t *>(&value), sizeof(value)); }
void BLECharacteristic::setValue(int value) { setValue(static_cast<uint32_t>(value)); }
void BLECharacteristic::setValue(float value) { setValue(reinterpret_cast<const uint8_t *>(&value), sizeof(value)); }
void BLECharacteristic::setValue(double value) { setValue(reinterpret_cast<const uint8_t *>(&value), sizeof(value)); }

const uint8_t *BLECharacteristic::getValue(size_t *length) const {
  if (!_impl || _impl->value.empty()) { if (length) *length = 0; return nullptr; }
  if (length) *length = _impl->value.size();
  return _impl->value.data();
}

String BLECharacteristic::getStringValue() const {
  if (!_impl || _impl->value.empty()) return "";
  return String(reinterpret_cast<const char *>(_impl->value.data()), _impl->value.size());
}

BTStatus BLECharacteristic::notify(const uint8_t *, size_t) { return BTStatus::NotSupported; }
BTStatus BLECharacteristic::notify(uint16_t, const uint8_t *, size_t) { return BTStatus::NotSupported; }
BTStatus BLECharacteristic::indicate(const uint8_t *, size_t) { return BTStatus::NotSupported; }
BTStatus BLECharacteristic::indicate(uint16_t, const uint8_t *, size_t) { return BTStatus::NotSupported; }

BLEProperty BLECharacteristic::getProperties() const { return _impl ? _impl->properties : static_cast<BLEProperty>(0); }
void BLECharacteristic::setPermissions(BLEPermission perms) { BLE_CHECK_IMPL(); impl.permissions = perms; }
BLEPermission BLECharacteristic::getPermissions() const { return _impl ? _impl->permissions : static_cast<BLEPermission>(0); }

BTStatus BLECharacteristic::onRead(ReadHandler) { return BTStatus::NotSupported; }
BTStatus BLECharacteristic::onWrite(WriteHandler) { return BTStatus::NotSupported; }
BTStatus BLECharacteristic::onNotify(NotifyHandler) { return BTStatus::NotSupported; }
BTStatus BLECharacteristic::onSubscribe(SubscribeHandler) { return BTStatus::NotSupported; }
BTStatus BLECharacteristic::onStatus(StatusHandler) { return BTStatus::NotSupported; }

BLEDescriptor BLECharacteristic::createDescriptor(const BLEUUID &uuid, BLEPermission perms, size_t maxLen) {
  BLE_CHECK_IMPL(BLEDescriptor());
  auto descImpl = std::make_shared<BLEDescriptor::Impl>();
  descImpl->uuid = uuid;
  descImpl->charImpl = _impl;
  descImpl->value.reserve(maxLen);
  impl.descriptors.push_back(descImpl);
  return BLEDescriptor(descImpl);
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
  auto &v = _impl->descriptors;
  v.erase(std::remove(v.begin(), v.end(), desc._impl), v.end());
}

size_t BLECharacteristic::getSubscribedCount() const { return 0; }
std::vector<uint16_t> BLECharacteristic::getSubscribedConnections() const { return {}; }
bool BLECharacteristic::isSubscribed(uint16_t) const { return false; }

BLEUUID BLECharacteristic::getUUID() const { return _impl ? _impl->uuid : BLEUUID(); }
uint16_t BLECharacteristic::getHandle() const { return _impl ? _impl->handle : 0; }

BLEService BLECharacteristic::getService() const {
  return _impl ? BLEService(_impl->serviceImpl.lock()) : BLEService();
}

void BLECharacteristic::setDescription(const String &) {}
String BLECharacteristic::toString() const { return _impl ? "BLECharacteristic(uuid=" + _impl->uuid.toString() + ")" : "BLECharacteristic(null)"; }

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
