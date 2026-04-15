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

#include "impl/BLEServiceBackend.h"
#include "impl/BLECharacteristicBackend.h"
#include "impl/BLEImplHelpers.h"
#if BLE_NIMBLE
#include "impl/nimble/NimBLEUUID.h"
#endif

// --------------------------------------------------------------------------
// BLEService common API (stack-agnostic)
// --------------------------------------------------------------------------

BLEService::BLEService() : _impl(nullptr) {}
BLEService::operator bool() const { return _impl != nullptr; }

BLECharacteristic BLEService::getCharacteristic(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLECharacteristic());
  for (auto &chr : impl.characteristics) {
    if (chr->uuid == uuid) {
      return BLECharacteristic(chr);
    }
  }
  return BLECharacteristic();
}

std::vector<BLECharacteristic> BLEService::getCharacteristics() const {
  std::vector<BLECharacteristic> result;
  BLE_CHECK_IMPL(result);
  result.reserve(impl.characteristics.size());
  for (auto &chr : impl.characteristics) {
    result.push_back(BLECharacteristic(chr));
  }
  return result;
}

void BLEService::removeCharacteristic(const BLECharacteristic &chr) {
  if (!_impl || !chr._impl) return;
  auto &chars = _impl->characteristics;
  for (auto it = chars.begin(); it != chars.end(); ++it) {
    if (*it == chr._impl) {
      chars.erase(it);
      break;
    }
  }
}

bool BLEService::isStarted() const { return _impl && _impl->started; }
BLEUUID BLEService::getUUID() const { return _impl ? _impl->uuid : BLEUUID(); }
uint16_t BLEService::getHandle() const { return _impl ? _impl->handle : 0; }

BLEServer BLEService::getServer() const {
  return _impl && _impl->server ? BLEServer(std::shared_ptr<BLEServer::Impl>(_impl->server, [](BLEServer::Impl *){})) : BLEServer();
}

BLECharacteristic BLEService::createCharacteristic(const BLEUUID &uuid, BLEProperty properties) {
  BLE_CHECK_IMPL(BLECharacteristic());

  for (auto &chr : impl.characteristics) {
    if (chr->uuid == uuid) {
      return BLECharacteristic(chr);
    }
  }

  auto chr = std::make_shared<BLECharacteristic::Impl>();
  chr->uuid = uuid;
  chr->properties = properties;
  chr->service = _impl.get();
#if BLE_NIMBLE
  uuidToNimble(uuid, chr->nimbleUUID);
#endif

  BLEPermission perms{};
  if (properties & BLEProperty::Read) perms = perms | BLEPermission::Read;
  if ((properties & BLEProperty::Write) || (properties & BLEProperty::WriteNR))
    perms = perms | BLEPermission::Write;
  chr->permissions = perms;

  impl.characteristics.push_back(chr);
  return BLECharacteristic(chr);
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

#endif /* BLE_ENABLED */
