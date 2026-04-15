/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
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

#include "BLEScan.h"
#include "impl/BLEImplHelpers.h"
#include "impl/BLEScanBackend.h"
#include "impl/BLEMutex.h"
#include "esp32-hal-log.h"

#include <utility>

BLEScan::BLEScan() : _impl(nullptr) {}

BLEScan::operator bool() const {
  BLE_CHECK_IMPL(false);
  (void)impl;
  return true;
}

void BLEScan::setInterval(uint16_t intervalMs) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL();
  log_d("Scan: setInterval %u ms", intervalMs);
  impl.interval = (intervalMs * 1000) / 625;
#endif
}

void BLEScan::setWindow(uint16_t windowMs) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL();
  log_d("Scan: setWindow %u ms", windowMs);
  impl.window = (windowMs * 1000) / 625;
#endif
}

void BLEScan::setActiveScan(bool active) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL();
  log_d("Scan: setActiveScan=%d", active);
  impl.activeScan = active;
#endif
}

void BLEScan::setFilterDuplicates(bool filter) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL();
  log_d("Scan: setFilterDuplicates=%d", filter);
  impl.filterDuplicates = filter;
#endif
}

bool BLEScan::isScanning() const { return _impl && _impl->scanning; }

void BLEScan::onResult(ResultHandler callback) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL();
  impl.onResultCb = callback;
#else
  (void)callback;
  log_w("%s not supported (no BLE scan backend)", __func__);
#endif
}

void BLEScan::onComplete(CompleteHandler callback) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL();
  impl.onCompleteCb = callback;
#else
  (void)callback;
  log_w("%s not supported (no BLE scan backend)", __func__);
#endif
}

void BLEScan::setCallbacks(Callbacks &callbacks) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL();
  impl.callbacks = &callbacks;
#else
  (void)callbacks;
  log_w("%s not supported (no BLE scan backend)", __func__);
#endif
}

void BLEScan::resetCallbacks() {
#if BLE_SCAN_BACKEND_AVAILABLE
  if (!_impl) return;
  auto &impl = *_impl;
  impl.callbacks = nullptr;
#endif
}

void BLEScan::onPeriodicSync(PeriodicSyncHandler handler) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL();
  impl.periodicSyncCb = handler;
#else
  (void)handler;
  log_w("%s not supported (no BLE scan backend)", __func__);
#endif
}

void BLEScan::onPeriodicReport(PeriodicReportHandler handler) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL();
  impl.periodicReportCb = handler;
#else
  (void)handler;
  log_w("%s not supported (no BLE scan backend)", __func__);
#endif
}

void BLEScan::onPeriodicLost(PeriodicLostHandler handler) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL();
  impl.periodicLostCb = handler;
#else
  (void)handler;
  log_w("%s not supported (no BLE scan backend)", __func__);
#endif
}

#if BLE_SCAN_BACKEND_AVAILABLE

BLEScanResults BLEScan::getResults() {
  if (!_impl) return BLEScanResults();
  BLELockGuard lock(_impl->mtx);
  return _impl->results;
}

void BLEScan::clearResults() {
  BLE_CHECK_IMPL();
  BLELockGuard lock(impl.mtx);
  impl.results = BLEScanResults();
}

void BLEScan::erase(const BTAddress &address) {
  BLE_CHECK_IMPL();
  BLELockGuard lock(impl.mtx);
  auto &devs = impl.results._devices;
  for (auto it = devs.begin(); it != devs.end(); ++it) {
    if (it->getAddress() == address) {
      devs.erase(it);
      return;
    }
  }
}

#endif /* BLE_SCAN_BACKEND_AVAILABLE */

#endif /* BLE_ENABLED */
