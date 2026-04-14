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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)

#include "BLEScan.h"
#include "impl/BLEImplHelpers.h"
#include "impl/BLEScanBackend.h"

#include <utility>

BLEScan::BLEScan() : _impl(nullptr) {}

BLEScan::operator bool() const {
  BLE_CHECK_IMPL(false);
  (void)impl;
  return true;
}

BTStatus BLEScan::onResult(ResultHandler callback) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onResultCb = callback;
  return BTStatus::OK;
#else
  (void)callback;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEScan::onComplete(CompleteHandler callback) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onCompleteCb = callback;
  return BTStatus::OK;
#else
  (void)callback;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEScan::setCallbacks(Callbacks &callbacks) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.callbacks = &callbacks;
  return BTStatus::OK;
#else
  (void)callbacks;
  return BTStatus::NotSupported;
#endif
}

void BLEScan::resetCallbacks() {
#if BLE_SCAN_BACKEND_AVAILABLE
  if (!_impl) return;
  auto &impl = *_impl;
  impl.callbacks = nullptr;
#endif
}

BTStatus BLEScan::onPeriodicSync(PeriodicSyncHandler handler) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.periodicSyncCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEScan::onPeriodicReport(PeriodicReportHandler handler) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.periodicReportCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEScan::onPeriodicLost(PeriodicLostHandler handler) {
#if BLE_SCAN_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.periodicLostCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
