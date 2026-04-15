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

#include "impl/BLEAdvertisingBackend.h"
#include "impl/BLEImplHelpers.h"

#include <algorithm>

// --------------------------------------------------------------------------
// BLEAdvertising common API (stack-agnostic)
// --------------------------------------------------------------------------

BLEAdvertising::BLEAdvertising() : _impl(nullptr) {}
BLEAdvertising::operator bool() const { return _impl != nullptr; }

// --------------------------------------------------------------------------
// Service UUID management
// --------------------------------------------------------------------------

void BLEAdvertising::addServiceUUID(const BLEUUID &uuid) {
  BLE_CHECK_IMPL();
  impl.serviceUUIDs.push_back(uuid);
}

void BLEAdvertising::removeServiceUUID(const BLEUUID &uuid) {
  BLE_CHECK_IMPL();
  auto &v = impl.serviceUUIDs;
  v.erase(std::remove(v.begin(), v.end(), uuid), v.end());
}

void BLEAdvertising::clearServiceUUIDs() {
  BLE_CHECK_IMPL();
  impl.serviceUUIDs.clear();
}

// --------------------------------------------------------------------------
// isAdvertising
// --------------------------------------------------------------------------

bool BLEAdvertising::isAdvertising() const { return _impl && _impl->advertising; }

// --------------------------------------------------------------------------
// Extended / Periodic advertising stubs (BLE5 -- not yet supported)
// --------------------------------------------------------------------------

BTStatus BLEAdvertising::configureExtended(const ExtAdvConfig &) { return BTStatus::NotSupported; }
BTStatus BLEAdvertising::setExtAdvertisementData(uint8_t, const BLEAdvertisementData &) { return BTStatus::NotSupported; }
BTStatus BLEAdvertising::setExtScanResponseData(uint8_t, const BLEAdvertisementData &) { return BTStatus::NotSupported; }
BTStatus BLEAdvertising::setExtInstanceAddress(uint8_t, const BTAddress &) { return BTStatus::NotSupported; }
BTStatus BLEAdvertising::startExtended(uint8_t, uint32_t, uint8_t) { return BTStatus::NotSupported; }
BTStatus BLEAdvertising::stopExtended(uint8_t) { return BTStatus::NotSupported; }
BTStatus BLEAdvertising::removeExtended(uint8_t) { return BTStatus::NotSupported; }
BTStatus BLEAdvertising::clearExtended() { return BTStatus::NotSupported; }

BTStatus BLEAdvertising::configurePeriodicAdv(const PeriodicAdvConfig &) { return BTStatus::NotSupported; }
BTStatus BLEAdvertising::setPeriodicAdvData(uint8_t, const BLEAdvertisementData &) { return BTStatus::NotSupported; }
BTStatus BLEAdvertising::startPeriodicAdv(uint8_t) { return BTStatus::NotSupported; }
BTStatus BLEAdvertising::stopPeriodicAdv(uint8_t) { return BTStatus::NotSupported; }

#endif /* BLE_ENABLED */
