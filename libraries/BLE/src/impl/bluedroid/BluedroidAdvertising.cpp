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
#include "BluedroidAdvertising.h"

#include "impl/BLESync.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <esp_gap_ble_api.h>
#include <esp_bt_defs.h>
#include <string.h>

static const uint32_t ADV_SYNC_TIMEOUT_MS = 2000;

// --------------------------------------------------------------------------
// Static instance for event routing
// --------------------------------------------------------------------------

BLEAdvertising::Impl *BLEAdvertising::Impl::s_instance = nullptr;

// --------------------------------------------------------------------------
// Helpers: map BLEAdvType to Bluedroid esp_ble_adv_type_t
// --------------------------------------------------------------------------

static esp_ble_adv_type_t mapAdvType(BLEAdvType type) {
  switch (type) {
    case BLEAdvType::ConnectableScannable: return ADV_TYPE_IND;
    case BLEAdvType::Connectable:          return ADV_TYPE_IND;
    case BLEAdvType::ConnectableDirected:  return ADV_TYPE_DIRECT_IND_HIGH;
    case BLEAdvType::DirectedHighDuty:     return ADV_TYPE_DIRECT_IND_HIGH;
    case BLEAdvType::DirectedLowDuty:      return ADV_TYPE_DIRECT_IND_LOW;
    case BLEAdvType::ScannableUndirected:  return ADV_TYPE_SCAN_IND;
    case BLEAdvType::NonConnectable:       return ADV_TYPE_NONCONN_IND;
    default:                               return ADV_TYPE_IND;
  }
}

static esp_ble_adv_filter_t mapFilterPolicy(bool scanWL, bool connectWL) {
  if (scanWL && connectWL) return ADV_FILTER_ALLOW_SCAN_WLST_CON_WLST;
  if (scanWL)              return ADV_FILTER_ALLOW_SCAN_WLST_CON_ANY;
  if (connectWL)           return ADV_FILTER_ALLOW_SCAN_ANY_CON_WLST;
  return ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
}

// --------------------------------------------------------------------------
// Pack service UUIDs into a flat byte buffer (little-endian, native sizes)
// --------------------------------------------------------------------------

static std::vector<uint8_t> packServiceUUIDs(const std::vector<BLEUUID> &uuids) {
  std::vector<uint8_t> buf;
  for (const auto &uuid : uuids) {
    switch (uuid.bitSize()) {
      case 16: {
        // data() is big-endian 16 bytes; 16-bit value is at [2..3] big-endian
        buf.push_back(uuid.data()[3]);
        buf.push_back(uuid.data()[2]);
        break;
      }
      case 32: {
        buf.push_back(uuid.data()[3]);
        buf.push_back(uuid.data()[2]);
        buf.push_back(uuid.data()[1]);
        buf.push_back(uuid.data()[0]);
        break;
      }
      default: {
        // 128-bit: stored big-endian, Bluedroid needs little-endian
        BLEUUID u128 = uuid.to128();
        const uint8_t *be = u128.data();
        for (int i = 15; i >= 0; i--) {
          buf.push_back(be[i]);
        }
        break;
      }
    }
  }
  return buf;
}

// --------------------------------------------------------------------------
// GAP event handler
// --------------------------------------------------------------------------

void BLEAdvertising::Impl::handleGAP(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  auto *inst = s_instance;
  if (!inst) return;

  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
      log_d("ADV_DATA_SET_COMPLETE status=%d", param->adv_data_cmpl.status);
      inst->advSync.give(param->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS ? BTStatus::OK : BTStatus::Fail);
      break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
      log_d("SCAN_RSP_DATA_SET_COMPLETE status=%d", param->scan_rsp_data_cmpl.status);
      inst->advSync.give(param->scan_rsp_data_cmpl.status == ESP_BT_STATUS_SUCCESS ? BTStatus::OK : BTStatus::Fail);
      break;

    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
      log_d("ADV_DATA_RAW_SET_COMPLETE status=%d", param->adv_data_raw_cmpl.status);
      inst->advSync.give(param->adv_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS ? BTStatus::OK : BTStatus::Fail);
      break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
      log_d("SCAN_RSP_DATA_RAW_SET_COMPLETE status=%d", param->scan_rsp_data_raw_cmpl.status);
      inst->advSync.give(param->scan_rsp_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS ? BTStatus::OK : BTStatus::Fail);
      break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      log_d("ADV_START_COMPLETE status=%d", param->adv_start_cmpl.status);
      if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
        inst->advertising = true;
      }
      inst->advSync.give(param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS ? BTStatus::OK : BTStatus::Fail);
      break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT: {
      log_d("ADV_STOP_COMPLETE status=%d", param->adv_stop_cmpl.status);
      inst->advertising = false;
      inst->advSync.give(param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS ? BTStatus::OK : BTStatus::Fail);
      if (inst->onCompleteCb) {
        inst->onCompleteCb(0);
      }
      break;
    }

    default:
      break;
  }
}

// --------------------------------------------------------------------------
// Configuration setters
// --------------------------------------------------------------------------

void BLEAdvertising::setName(const String &name) {
  BLE_CHECK_IMPL();
  impl.name = name;
}

void BLEAdvertising::setScanResponse(bool enable) {
  BLE_CHECK_IMPL();
  impl.scanResp = enable;
}

void BLEAdvertising::setType(BLEAdvType type) {
  BLE_CHECK_IMPL();
  impl.advType = type;
}

void BLEAdvertising::setInterval(uint16_t minMs, uint16_t maxMs) {
  BLE_CHECK_IMPL();
  // Convert ms to 0.625ms units: val = ms / 0.625 = ms * 8 / 5
  impl.minInterval = (uint16_t)((uint32_t)minMs * 8 / 5);
  impl.maxInterval = (uint16_t)((uint32_t)maxMs * 8 / 5);
}

void BLEAdvertising::setMinPreferred(uint16_t val) {
  BLE_CHECK_IMPL();
  impl.minPreferred = val;
}

void BLEAdvertising::setMaxPreferred(uint16_t val) {
  BLE_CHECK_IMPL();
  impl.maxPreferred = val;
}

void BLEAdvertising::setTxPower(bool include) {
  BLE_CHECK_IMPL();
  impl.includeTxPower = include;
}

void BLEAdvertising::setAppearance(uint16_t appearance) {
  BLE_CHECK_IMPL();
  impl.appearance = appearance;
}

void BLEAdvertising::setScanFilter(bool scanRequestWhitelistOnly, bool connectWhitelistOnly) {
  BLE_CHECK_IMPL();
  impl.scanRequestWhitelistOnly = scanRequestWhitelistOnly;
  impl.connectWhitelistOnly = connectWhitelistOnly;
}

void BLEAdvertising::reset() {
  BLE_CHECK_IMPL();
  if (impl.advertising) {
    stop();
  }
  impl.serviceUUIDs.clear();
  impl.name = "";
  impl.includeName = true;
  impl.scanResp = true;
  impl.advType = BLEAdvType::ConnectableScannable;
  impl.minInterval = 0x20;
  impl.maxInterval = 0x40;
  impl.minPreferred = 0;
  impl.maxPreferred = 0;
  impl.includeTxPower = false;
  impl.appearance = 0;
  impl.scanRequestWhitelistOnly = false;
  impl.connectWhitelistOnly = false;
  impl.customAdvData = false;
  impl.customScanRespData = false;
  impl.rawAdvData.clear();
  impl.rawScanRespData.clear();
}

// --------------------------------------------------------------------------
// Custom advertisement / scan response data
// --------------------------------------------------------------------------

void BLEAdvertising::setAdvertisementData(const BLEAdvertisementData &data) {
  BLE_CHECK_IMPL();
  impl.rawAdvData.assign(data.data(), data.data() + data.length());
  impl.customAdvData = true;
}

void BLEAdvertising::setScanResponseData(const BLEAdvertisementData &data) {
  BLE_CHECK_IMPL();
  impl.rawScanRespData.assign(data.data(), data.data() + data.length());
  impl.customScanRespData = true;
}

// --------------------------------------------------------------------------
// start()
// --------------------------------------------------------------------------

BTStatus BLEAdvertising::start(uint32_t durationMs) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);

  log_d("start: customAdv=%d, customScanResp=%d, scanResp=%d",
        impl.customAdvData, impl.customScanRespData, impl.scanResp);

  // Resolve the advertised device name
  String devName = impl.name.length() > 0 ? impl.name : BLE.getDeviceName();

  // --- Configure advertising data ---
  if (impl.customAdvData) {
    // Raw advertising data supplied by user
    impl.advSync.take();
    esp_err_t err = esp_ble_gap_config_adv_data_raw(impl.rawAdvData.data(), impl.rawAdvData.size());
    if (err != ESP_OK) {
      log_e("esp_ble_gap_config_adv_data_raw: %s", esp_err_to_name(err));
      return BTStatus::Fail;
    }
    BTStatus s = impl.advSync.wait(ADV_SYNC_TIMEOUT_MS);
    if (!s) {
      log_e("Timeout waiting for raw adv data set");
      return s;
    }

    // Raw scan response if provided
    if (impl.customScanRespData) {
      impl.advSync.take();
      err = esp_ble_gap_config_scan_rsp_data_raw(impl.rawScanRespData.data(), impl.rawScanRespData.size());
      if (err != ESP_OK) {
        log_e("esp_ble_gap_config_scan_rsp_data_raw: %s", esp_err_to_name(err));
        return BTStatus::Fail;
      }
      s = impl.advSync.wait(ADV_SYNC_TIMEOUT_MS);
      if (!s) {
        log_e("Timeout waiting for raw scan resp data set");
        return s;
      }
    }
  } else {
    // Build structured advertising data via esp_ble_adv_data_t
    std::vector<uint8_t> packedUUIDs = packServiceUUIDs(impl.serviceUUIDs);

    esp_ble_adv_data_t advData = {};
    advData.set_scan_rsp = false;
    advData.include_name = impl.includeName;
    advData.include_txpower = impl.includeTxPower;
    advData.min_interval = impl.minPreferred;
    advData.max_interval = impl.maxPreferred;
    advData.appearance = impl.appearance;
    advData.flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
    advData.p_service_uuid = packedUUIDs.empty() ? nullptr : packedUUIDs.data();
    advData.service_uuid_len = packedUUIDs.size();

    impl.advSync.take();
    esp_err_t err = esp_ble_gap_config_adv_data(&advData);
    if (err != ESP_OK) {
      log_e("esp_ble_gap_config_adv_data: %s", esp_err_to_name(err));
      return BTStatus::Fail;
    }
    BTStatus s = impl.advSync.wait(ADV_SYNC_TIMEOUT_MS);
    if (!s) {
      log_e("Timeout waiting for adv data set");
      return s;
    }

    // Configure scan response data if enabled
    if (impl.scanResp) {
      esp_ble_adv_data_t scanRespData = {};
      scanRespData.set_scan_rsp = true;
      scanRespData.include_name = impl.includeName;
      scanRespData.include_txpower = impl.includeTxPower;
      scanRespData.min_interval = impl.minPreferred;
      scanRespData.max_interval = impl.maxPreferred;
      scanRespData.appearance = impl.appearance;

      impl.advSync.take();
      err = esp_ble_gap_config_adv_data(&scanRespData);
      if (err != ESP_OK) {
        log_e("esp_ble_gap_config_adv_data (scan resp): %s", esp_err_to_name(err));
        return BTStatus::Fail;
      }
      s = impl.advSync.wait(ADV_SYNC_TIMEOUT_MS);
      if (!s) {
        log_e("Timeout waiting for scan resp data set");
        return s;
      }
    }
  }

  // --- Build advertising parameters ---
  esp_ble_adv_params_t advParams = {};
  advParams.adv_int_min = impl.minInterval;
  advParams.adv_int_max = impl.maxInterval;
  advParams.adv_type = mapAdvType(impl.advType);
  advParams.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  advParams.channel_map = ADV_CHNL_ALL;
  advParams.adv_filter_policy = mapFilterPolicy(impl.scanRequestWhitelistOnly, impl.connectWhitelistOnly);
  advParams.peer_addr_type = BLE_ADDR_TYPE_PUBLIC;

  // --- Start advertising ---
  impl.advSync.take();
  esp_err_t err = esp_ble_gap_start_advertising(&advParams);
  if (err != ESP_OK) {
    log_e("esp_ble_gap_start_advertising: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }
  BTStatus s = impl.advSync.wait(ADV_SYNC_TIMEOUT_MS);
  if (!s) {
    log_e("Timeout waiting for adv start");
    return s;
  }

  log_d("Advertising started");
  return BTStatus::OK;
}

// --------------------------------------------------------------------------
// stop()
// --------------------------------------------------------------------------

BTStatus BLEAdvertising::stop() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (!impl.advertising) {
    return BTStatus::OK;
  }

  impl.advSync.take();
  esp_err_t err = esp_ble_gap_stop_advertising();
  if (err != ESP_OK) {
    log_e("esp_ble_gap_stop_advertising: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }
  BTStatus s = impl.advSync.wait(ADV_SYNC_TIMEOUT_MS);
  if (!s) {
    log_e("Timeout waiting for adv stop");
    return s;
  }

  log_d("Advertising stopped");
  return BTStatus::OK;
}

// --------------------------------------------------------------------------
// onComplete
// --------------------------------------------------------------------------

BTStatus BLEAdvertising::onComplete(CompleteHandler h) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onCompleteCb = std::move(h);
  return BTStatus::OK;
}

// --------------------------------------------------------------------------
// BLEClass factory and shortcuts
// --------------------------------------------------------------------------

BLEAdvertising BLEClass::getAdvertising() {
  if (!isInitialized()) return BLEAdvertising();
  static std::shared_ptr<BLEAdvertising::Impl> advImpl;
  if (!advImpl) {
    advImpl = std::make_shared<BLEAdvertising::Impl>();
    BLEAdvertising::Impl::s_instance = advImpl.get();
  }
  return BLEAdvertising(advImpl);
}

BTStatus BLEClass::startAdvertising() { return getAdvertising().start(); }
BTStatus BLEClass::stopAdvertising() { return getAdvertising().stop(); }

#endif /* BLE_BLUEDROID */
