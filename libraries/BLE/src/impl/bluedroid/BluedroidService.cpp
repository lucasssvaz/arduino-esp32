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

#include "BluedroidService.h"
#include "impl/BLEImplHelpers.h"

// --------------------------------------------------------------------------
// BLEService public API -- Bluedroid
// --------------------------------------------------------------------------

BLEService::BLEService() : _impl(nullptr) {}
BLEService::operator bool() const { return _impl != nullptr; }

BLECharacteristic BLEService::createCharacteristic(const BLEUUID &, BLEProperty) { return BLECharacteristic(); }
BLECharacteristic BLEService::getCharacteristic(const BLEUUID &) { return BLECharacteristic(); }
std::vector<BLECharacteristic> BLEService::getCharacteristics() const { return {}; }
void BLEService::removeCharacteristic(const BLECharacteristic &) {}

BTStatus BLEService::start() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.started = true;
  return BTStatus::OK;
}
void BLEService::stop() { BLE_CHECK_IMPL(); impl.started = false; }
bool BLEService::isStarted() const { return _impl && _impl->started; }
BLEUUID BLEService::getUUID() const { return _impl ? _impl->uuid : BLEUUID(); }
uint16_t BLEService::getHandle() const { return _impl ? _impl->handle : 0; }

BLEServer BLEService::getServer() const {
  return _impl ? BLEServer(_impl->serverImpl.lock()) : BLEServer();
}

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
