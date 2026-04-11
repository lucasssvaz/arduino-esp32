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

#include "impl/BLESync.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <esp_gap_ble_api.h>
#include <string.h>

// --------------------------------------------------------------------------
// BLEAdvertising::Impl -- Bluedroid backend
// --------------------------------------------------------------------------

struct BLEAdvertising::Impl {
  bool advertising = false;
  BLESync advSync;
  BLEAdvertising::CompleteHandler onCompleteCb;
};

BLEAdvertising::BLEAdvertising() : _impl(nullptr) {}
BLEAdvertising::operator bool() const { return _impl != nullptr; }

void BLEAdvertising::addServiceUUID(const BLEUUID &) {}
void BLEAdvertising::removeServiceUUID(const BLEUUID &) {}
void BLEAdvertising::clearServiceUUIDs() {}

void BLEAdvertising::setName(const String &) {}
void BLEAdvertising::setScanResponse(bool) {}
void BLEAdvertising::setType(BLEAdvType) {}
void BLEAdvertising::setInterval(uint16_t, uint16_t) {}
void BLEAdvertising::setMinPreferred(uint16_t) {}
void BLEAdvertising::setMaxPreferred(uint16_t) {}
void BLEAdvertising::setTxPower(bool) {}
void BLEAdvertising::setAppearance(uint16_t) {}
void BLEAdvertising::setScanFilter(bool, bool) {}
void BLEAdvertising::reset() {}

void BLEAdvertising::setAdvertisementData(const BLEAdvertisementData &) {}
void BLEAdvertising::setScanResponseData(const BLEAdvertisementData &) {}

BTStatus BLEAdvertising::start(uint32_t) {
  return BTStatus::NotSupported;
}

BTStatus BLEAdvertising::stop() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.advertising = false;
  return BTStatus::OK;
}

bool BLEAdvertising::isAdvertising() const { return _impl && _impl->advertising; }

BTStatus BLEAdvertising::onComplete(CompleteHandler h) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onCompleteCb = std::move(h);
  return BTStatus::OK;
}

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

BLEAdvertising BLEClass::getAdvertising() {
  if (!isInitialized()) return BLEAdvertising();
  static std::shared_ptr<BLEAdvertising::Impl> advImpl;
  if (!advImpl) advImpl = std::make_shared<BLEAdvertising::Impl>();
  return BLEAdvertising(advImpl);
}

BTStatus BLEClass::startAdvertising() { return getAdvertising().start(); }
BTStatus BLEClass::stopAdvertising() { return getAdvertising().stop(); }

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
