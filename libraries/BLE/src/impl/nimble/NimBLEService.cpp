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

#include "BLE.h"

#include "NimBLECharacteristic.h"
#include "NimBLEServer.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

// --------------------------------------------------------------------------
// BLEService public API
// --------------------------------------------------------------------------

BLEService::BLEService() : _impl(nullptr) {}

BLEService::operator bool() const {
  return _impl != nullptr;
}

BLECharacteristic BLEService::createCharacteristic(const BLEUUID &uuid, BLEProperty properties) {
  BLE_CHECK_IMPL(BLECharacteristic());

  for (auto &chrImpl : impl.characteristics) {
    if (chrImpl->uuid == uuid) {
      return BLECharacteristic(chrImpl);
    }
  }

  auto chrImpl = std::make_shared<BLECharacteristic::Impl>();
  chrImpl->uuid = uuid;
  chrImpl->properties = properties;
  chrImpl->serviceImpl = _impl.get();
  uuidToNimble(uuid, chrImpl->nimbleUUID);

  BLEPermission perms{};
  if (properties & BLEProperty::Read) perms = perms | BLEPermission::Read;
  if ((properties & BLEProperty::Write) || (properties & BLEProperty::WriteNR)) perms = perms | BLEPermission::Write;
  chrImpl->permissions = perms;

  impl.characteristics.push_back(chrImpl);

  return BLECharacteristic(chrImpl);
}

BLECharacteristic BLEService::getCharacteristic(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLECharacteristic());
  for (auto &chrImpl : impl.characteristics) {
    if (chrImpl->uuid == uuid) {
      return BLECharacteristic(chrImpl);
    }
  }
  return BLECharacteristic();
}

std::vector<BLECharacteristic> BLEService::getCharacteristics() const {
  std::vector<BLECharacteristic> result;
  BLE_CHECK_IMPL(result);
  for (auto &chrImpl : impl.characteristics) {
    result.push_back(BLECharacteristic(chrImpl));
  }
  return result;
}

void BLEService::removeCharacteristic(const BLECharacteristic &chr) {
  if (!_impl || !chr._impl) return;
  auto &v = _impl->characteristics;
  for (auto it = v.begin(); it != v.end(); ++it) {
    if (*it == chr._impl) {
      v.erase(it);
      break;
    }
  }
}

BTStatus BLEService::start() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.started = true;
  return BTStatus::OK;
}

void BLEService::stop() {
  BLE_CHECK_IMPL();
  impl.started = false;
}

bool BLEService::isStarted() const {
  return _impl && _impl->started;
}

BLEUUID BLEService::getUUID() const {
  return _impl ? _impl->uuid : BLEUUID();
}

uint16_t BLEService::getHandle() const {
  return _impl ? _impl->handle : 0;
}

BLEServer BLEService::getServer() const {
  return _impl && _impl->serverImpl ? BLEServer(std::shared_ptr<BLEServer::Impl>(_impl->serverImpl, [](BLEServer::Impl *){})) : BLEServer();
}

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
