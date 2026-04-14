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
#include <vector>

struct BLECharacteristic::Impl {
  BLEUUID uuid;
  uint16_t handle = 0;
  BLEProperty properties = static_cast<BLEProperty>(0);
  BLEPermission permissions = static_cast<BLEPermission>(0);
  std::vector<uint8_t> value;
  std::vector<std::shared_ptr<BLEDescriptor::Impl>> descriptors;
  BLEService::Impl *serviceImpl = nullptr;
};

// --------------------------------------------------------------------------
// BLECharacteristic -- Bluedroid
// --------------------------------------------------------------------------

void BLECharacteristic::setValue(const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL(); impl.value.assign(data, data + length);
}
void BLECharacteristic::setValue(int value) { setValue(static_cast<uint32_t>(value)); }

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

BTStatus BLECharacteristic::onRead(ReadHandler) { return BTStatus::NotSupported; }
BTStatus BLECharacteristic::onWrite(WriteHandler) { return BTStatus::NotSupported; }
BTStatus BLECharacteristic::onNotify(NotifyHandler) { return BTStatus::NotSupported; }
BTStatus BLECharacteristic::onSubscribe(SubscribeHandler) { return BTStatus::NotSupported; }
BTStatus BLECharacteristic::onStatus(StatusHandler) { return BTStatus::NotSupported; }

BLEDescriptor BLECharacteristic::createDescriptor(const BLEUUID &uuid, BLEPermission perms, size_t maxLen) {
  BLE_CHECK_IMPL(BLEDescriptor());
  auto descImpl = std::make_shared<BLEDescriptor::Impl>();
  descImpl->uuid = uuid;
  descImpl->charImpl = _impl.get();
  descImpl->value.reserve(maxLen);
  impl.descriptors.push_back(descImpl);
  return BLEDescriptor(descImpl);
}

size_t BLECharacteristic::getSubscribedCount() const { return 0; }
std::vector<uint16_t> BLECharacteristic::getSubscribedConnections() const { return {}; }
bool BLECharacteristic::isSubscribed(uint16_t) const { return false; }

void BLECharacteristic::setDescription(const String &) {}

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
