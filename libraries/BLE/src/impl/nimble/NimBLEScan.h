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

#pragma once

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if (defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)) && defined(CONFIG_NIMBLE_ENABLED)

#include "BLEScan.h"
#include "impl/BLESync.h"

#include <host/ble_gap.h>

struct BLEScan::Impl {
  uint16_t interval = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
  uint16_t window = BLE_GAP_SCAN_FAST_WINDOW;
  bool activeScan = true;
  bool filterDuplicates = true;
  bool scanning = false;

  BLEScan::ResultHandler onResultCb = nullptr;
  BLEScan::CompleteHandler onCompleteCb = nullptr;
  BLEScan::Callbacks *callbacks = nullptr;

  BLEScanResults results;
  BLESync scanSync;

  BLEScan::PeriodicSyncHandler periodicSyncCb = nullptr;
  BLEScan::PeriodicReportHandler periodicReportCb = nullptr;
  BLEScan::PeriodicLostHandler periodicLostCb = nullptr;

  static int gapEventHandler(struct ble_gap_event *event, void *arg);

  BLEAdvertisedDevice parseDiscEvent(const struct ble_gap_disc_desc *disc);
#if CONFIG_BT_NIMBLE_EXT_ADV
  BLEAdvertisedDevice parseExtDiscEvent(const struct ble_gap_ext_disc_desc *disc);
#endif
};

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
