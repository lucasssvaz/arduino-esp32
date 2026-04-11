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

// --------------------------------------------------------------------------
// BLEScan::Impl -- Bluedroid backend
// --------------------------------------------------------------------------

struct BLEScan::Impl {
  uint16_t interval = 0x50;
  uint16_t window = 0x30;
  bool activeScan = true;
  bool filterDuplicates = true;
  bool scanning = false;
  BLEScanResults results;
  BLESync scanSync;

  std::function<void(BLEAdvertisedDevice)> onResultCb;
  std::function<void(BLEScanResults &)> onCompleteCb;
  PeriodicSyncHandler periodicSyncCb;
  PeriodicReportHandler periodicReportCb;
  PeriodicLostHandler periodicLostCb;
};

// --------------------------------------------------------------------------
// BLEScan public API -- Bluedroid
// --------------------------------------------------------------------------

BLEScan::BLEScan() : _impl(nullptr) {}
BLEScan::operator bool() const { return _impl != nullptr; }

void BLEScan::setInterval(uint16_t intervalMs) { BLE_CHECK_IMPL(); impl.interval = (intervalMs * 1000) / 625; }
void BLEScan::setWindow(uint16_t windowMs) { BLE_CHECK_IMPL(); impl.window = (windowMs * 1000) / 625; }
void BLEScan::setActiveScan(bool active) { BLE_CHECK_IMPL(); impl.activeScan = active; }
void BLEScan::setFilterDuplicates(bool filter) { BLE_CHECK_IMPL(); impl.filterDuplicates = filter; }
void BLEScan::clearDuplicateCache() {}

BTStatus BLEScan::start(uint32_t /*durationMs*/, bool /*continueExisting*/) {
  return BTStatus::NotSupported;
}

BLEScanResults BLEScan::startBlocking(uint32_t /*durationMs*/) {
  return BLEScanResults();
}

BTStatus BLEScan::stop() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.scanning = false;
  return BTStatus::OK;
}

bool BLEScan::isScanning() const { return _impl && _impl->scanning; }

BTStatus BLEScan::onResult(std::function<void(BLEAdvertisedDevice)> cb) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onResultCb = std::move(cb);
  return BTStatus::OK;
}

BTStatus BLEScan::onComplete(std::function<void(BLEScanResults &)> cb) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onCompleteCb = std::move(cb);
  return BTStatus::OK;
}

BLEScanResults BLEScan::getResults() { return _impl ? _impl->results : BLEScanResults(); }
void BLEScan::clearResults() { BLE_CHECK_IMPL(); impl.results = BLEScanResults(); }
void BLEScan::erase(const BTAddress &) {}

BTStatus BLEScan::startExtended(uint32_t, const ExtScanConfig *, const ExtScanConfig *) { return BTStatus::NotSupported; }
BTStatus BLEScan::stopExtended() { return stop(); }
BTStatus BLEScan::createPeriodicSync(const BTAddress &, uint8_t, uint16_t, uint16_t) { return BTStatus::NotSupported; }
BTStatus BLEScan::cancelPeriodicSync() { return BTStatus::NotSupported; }
BTStatus BLEScan::terminatePeriodicSync(uint16_t) { return BTStatus::NotSupported; }

BTStatus BLEScan::onPeriodicSync(PeriodicSyncHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.periodicSyncCb = std::move(h); return BTStatus::OK; }
BTStatus BLEScan::onPeriodicReport(PeriodicReportHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.periodicReportCb = std::move(h); return BTStatus::OK; }
BTStatus BLEScan::onPeriodicLost(PeriodicLostHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.periodicLostCb = std::move(h); return BTStatus::OK; }

BLEScan BLEClass::getScan() {
  if (!isInitialized()) return BLEScan();
  static std::shared_ptr<BLEScan::Impl> scanImpl;
  if (!scanImpl) scanImpl = std::make_shared<BLEScan::Impl>();
  return BLEScan(scanImpl);
}

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
