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

#include "impl/BLEGuards.h"
#if BLE_BLUEDROID

#include "BLE.h"

#include "BluedroidScan.h"
#include "impl/BLEAdvertisedDeviceImpl.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <esp_gap_ble_api.h>

BLEScan::Impl *BLEScan::Impl::s_instance = nullptr;

// --------------------------------------------------------------------------
// Configuration
// --------------------------------------------------------------------------

void BLEScan::clearDuplicateCache() {
#ifdef ESP_BLE_DUPLICATE_SCAN_EXCEPTIONAL_LIST_CLEAN_ALL
  esp_ble_gap_clean_duplicate_scan_exceptional_list(ESP_BLE_DUPLICATE_SCAN_EXCEPTIONAL_LIST_CLEAN_ALL, NULL, 0);
#endif
}

// --------------------------------------------------------------------------
// Start / stop
// --------------------------------------------------------------------------

BTStatus BLEScan::start(uint32_t durationMs, bool continueExisting) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);

  if (!continueExisting) {
    BLELockGuard lock(impl.mtx);
    impl.results = BLEScanResults();
  }

  // Configure scan parameters
  esp_ble_scan_params_t scanParams = {};
  scanParams.scan_type = impl.activeScan ? BLE_SCAN_TYPE_ACTIVE : BLE_SCAN_TYPE_PASSIVE;
  scanParams.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  scanParams.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
  scanParams.scan_interval = impl.interval;
  scanParams.scan_window = impl.window;
  scanParams.scan_duplicate = impl.filterDuplicates ? BLE_SCAN_DUPLICATE_ENABLE : BLE_SCAN_DUPLICATE_DISABLE;

  // Set scan params and wait for completion event
  impl.scanSync.take();
  esp_err_t err = esp_ble_gap_set_scan_params(&scanParams);
  if (err != ESP_OK) {
    log_e("esp_ble_gap_set_scan_params: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  BTStatus rc = impl.scanSync.wait(5000);
  if (rc != BTStatus::OK) {
    log_e("Scan param set did not complete (status=%d)", static_cast<int>(rc));
    return rc;
  }

  // Bluedroid uses seconds; convert ms -> s, 0 means infinite
  uint32_t durationSec = durationMs / 1000;

  err = esp_ble_gap_start_scanning(durationSec);
  if (err != ESP_OK) {
    log_e("esp_ble_gap_start_scanning: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  impl.scanning = true;
  return BTStatus::OK;
}

BLEScanResults BLEScan::startBlocking(uint32_t durationMs) {
  BLE_CHECK_IMPL(BLEScanResults());

  BTStatus rc = start(durationMs, false);
  if (rc != BTStatus::OK) {
    return BLEScanResults();
  }

  // Wait for scan completion (INQ_CMPL_EVT)
  impl.scanSync.take();
  uint32_t waitMs = (durationMs == 0) ? portMAX_DELAY : durationMs + 2000;
  impl.scanSync.wait(waitMs);

  BLELockGuard lock(impl.mtx);
  return impl.results;
}

BTStatus BLEScan::stop() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  esp_err_t err = esp_ble_gap_stop_scanning();
  if (err != ESP_OK) {
    log_e("esp_ble_gap_stop_scanning: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }
  impl.scanning = false;
  return BTStatus::OK;
}

// --------------------------------------------------------------------------
// Extended / Periodic (not supported on Bluedroid legacy)
// --------------------------------------------------------------------------

BTStatus BLEScan::startExtended(uint32_t, const ExtScanConfig *, const ExtScanConfig *) { return BTStatus::NotSupported; }
BTStatus BLEScan::stopExtended() { return stop(); }
BTStatus BLEScan::createPeriodicSync(const BTAddress &, uint8_t, uint16_t, uint16_t) { return BTStatus::NotSupported; }
BTStatus BLEScan::cancelPeriodicSync() { return BTStatus::NotSupported; }
BTStatus BLEScan::terminatePeriodicSync(uint16_t) { return BTStatus::NotSupported; }

// --------------------------------------------------------------------------
// GAP event handler (called from BluedroidCore gapCallback)
// --------------------------------------------------------------------------

void BLEScan::Impl::handleGAP(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  auto *inst = s_instance;
  if (!inst) return;

  switch (event) {

    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
      log_d("SCAN_PARAM_SET_COMPLETE status=%d", param->scan_param_cmpl.status);
      inst->scanSync.give(param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS ? BTStatus::OK : BTStatus::Fail);
      break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
      log_d("SCAN_START_COMPLETE status=%d", param->scan_start_cmpl.status);
      if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
        inst->scanning = false;
      }
      break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
      log_d("SCAN_STOP_COMPLETE status=%d", param->scan_stop_cmpl.status);
      inst->scanning = false;
      break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
      switch (param->scan_rst.search_evt) {

        case ESP_GAP_SEARCH_INQ_RES_EVT: {
          if (!inst->scanning) break;

          // Build advertised device from scan result
          auto devImpl = std::make_shared<BLEAdvertisedDevice::Impl>();
          devImpl->address = BTAddress(param->scan_rst.bda,
                                       static_cast<BTAddress::Type>(param->scan_rst.ble_addr_type));
          devImpl->addrType = static_cast<BTAddress::Type>(param->scan_rst.ble_addr_type);
          devImpl->rssi = param->scan_rst.rssi;
          devImpl->hasRSSI = true;
          devImpl->legacy = true;

          switch (param->scan_rst.ble_evt_type) {
            case ESP_BLE_EVT_CONN_ADV:
              devImpl->advType = BLEAdvType::ConnectableScannable;
              devImpl->connectable = true;
              devImpl->scannable = true;
              break;
            case ESP_BLE_EVT_CONN_DIR_ADV:
              devImpl->advType = BLEAdvType::ConnectableNonScannable;
              devImpl->connectable = true;
              devImpl->directed = true;
              break;
            case ESP_BLE_EVT_DISC_ADV:
              devImpl->advType = BLEAdvType::NonConnectableScannable;
              devImpl->scannable = true;
              break;
            case ESP_BLE_EVT_NON_CONN_ADV:
              devImpl->advType = BLEAdvType::NonConnectable;
              break;
            default:
              break;
          }

          devImpl->parsePayload(param->scan_rst.ble_adv,
                                param->scan_rst.adv_data_len + param->scan_rst.scan_rsp_len);

          BLEAdvertisedDevice dev(devImpl);

          // Snapshot callbacks under lock, invoke outside
          {
            ResultHandler resultCb;
            Callbacks *cbs = nullptr;
            bool isDuplicate = false;
            {
              BLELockGuard lock(inst->mtx);

              // If filtering duplicates, check if we already have this address
              if (inst->filterDuplicates) {
                for (const auto &existing : inst->results) {
                  if (existing.getAddress() == dev.getAddress()) {
                    isDuplicate = true;
                    break;
                  }
                }
              }

              if (!isDuplicate) {
                inst->results.appendOrReplace(dev);
                resultCb = inst->onResultCb;
                cbs = inst->callbacks;
              }
            }

            if (!isDuplicate) {
              if (resultCb) resultCb(dev);
              if (cbs) cbs->onResult(dev);
            }
          }
          break;
        }

        case ESP_GAP_SEARCH_INQ_CMPL_EVT: {
          log_d("SCAN_INQ_CMPL_EVT");
          inst->scanning = false;

          CompleteHandler completeCb;
          Callbacks *cbs;
          BLEScanResults resultsCopy;
          {
            BLELockGuard lock(inst->mtx);
            completeCb = inst->onCompleteCb;
            cbs = inst->callbacks;
            resultsCopy = inst->results;
          }

          if (completeCb) completeCb(resultsCopy);
          if (cbs) cbs->onComplete(resultsCopy);

          inst->scanSync.give();
          break;
        }

        default:
          break;
      }
      break;
    }

    default:
      break;
  }
}

// --------------------------------------------------------------------------
// getScan() factory
// --------------------------------------------------------------------------

BLEScan BLEClass::getScan() {
  if (!isInitialized()) return BLEScan();
  static std::shared_ptr<BLEScan::Impl> scanImpl;
  if (!scanImpl) {
    scanImpl = std::make_shared<BLEScan::Impl>();
    BLEScan::Impl::s_instance = scanImpl.get();
  }
  return BLEScan(scanImpl);
}

#endif /* BLE_BLUEDROID */
