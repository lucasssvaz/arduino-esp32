/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
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
#if BLE_NIMBLE

#include "BLE.h"
#include "NimBLEAdvertising.h"
#include "NimBLEServer.h"

#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <host/ble_hs.h>
#include <host/ble_gap.h>

int BLEAdvertising::Impl::gapEventCallback(struct ble_gap_event *event, void *arg) {
  auto *impl = static_cast<BLEAdvertising::Impl *>(arg);
  if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
    impl->advertising = false;
    if (impl->onCompleteCb) {
      impl->onCompleteCb(0);
    }
    return 0;
  }

  if (event->type == BLE_GAP_EVENT_CONNECT) {
    impl->advertising = false;
  }

  // Forward non-advertising GAP events to the server impl directly
  return nimbleServerForwardGapEvent(event);
}

// --------------------------------------------------------------------------
// BLEAdvertising public API (stack-specific)
// --------------------------------------------------------------------------

void BLEAdvertising::setName(const String &name) {
  BLE_CHECK_IMPL();
  impl.deviceName = name;
}

void BLEAdvertising::setScanResponse(bool enable) {
  BLE_CHECK_IMPL();
  impl.scanResponseEnabled = enable;
}

void BLEAdvertising::setType(BLEAdvType type) {
  BLE_CHECK_IMPL();
  impl.advType = type;
}

void BLEAdvertising::setInterval(uint16_t minMs, uint16_t maxMs) {
  BLE_CHECK_IMPL();
  impl.minInterval = (minMs * 1000) / 625;
  impl.maxInterval = (maxMs * 1000) / 625;
}

void BLEAdvertising::setMinPreferred(uint16_t v) { BLE_CHECK_IMPL(); impl.minPreferred = v; }
void BLEAdvertising::setMaxPreferred(uint16_t v) { BLE_CHECK_IMPL(); impl.maxPreferred = v; }
void BLEAdvertising::setTxPower(bool include) { BLE_CHECK_IMPL(); impl.includeTxPower = include; }
void BLEAdvertising::setAppearance(uint16_t appearance) { BLE_CHECK_IMPL(); impl.appearance = appearance; }

void BLEAdvertising::setScanFilter(bool scanWl, bool connWl) {
  BLE_CHECK_IMPL();
  impl.filterPolicy = 0;
  if (scanWl) impl.filterPolicy |= 0x01;
  if (connWl) impl.filterPolicy |= 0x02;
}

void BLEAdvertising::reset() {
  BLE_CHECK_IMPL();
  impl.serviceUUIDs.clear();
  impl.deviceName = "";
  impl.useCustomAdvData = false;
  impl.useCustomScanRsp = false;
  impl.customAdvData.clear();
  impl.customScanRspData.clear();
}

void BLEAdvertising::setAdvertisementData(const BLEAdvertisementData &data) {
  BLE_CHECK_IMPL();
  impl.customAdvData = data;
  impl.useCustomAdvData = true;
}

void BLEAdvertising::setScanResponseData(const BLEAdvertisementData &data) {
  BLE_CHECK_IMPL();
  impl.customScanRspData = data;
  impl.useCustomScanRsp = true;
}

BTStatus BLEAdvertising::start(uint32_t durationMs) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (impl.advertising) return BTStatus::OK;

  struct ble_gap_adv_params advParams = {};
  advParams.conn_mode = BLE_GAP_CONN_MODE_UND;
  advParams.disc_mode = BLE_GAP_DISC_MODE_GEN;
  advParams.itvl_min = impl.minInterval;
  advParams.itvl_max = impl.maxInterval;
  advParams.filter_policy = impl.filterPolicy;

  // Build advertisement data
  struct ble_hs_adv_fields fields = {};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  String name = impl.deviceName.length() > 0 ? impl.deviceName : BLE.getDeviceName();
  if (name.length() > 0) {
    fields.name = reinterpret_cast<const uint8_t *>(name.c_str());
    fields.name_len = name.length();
    fields.name_is_complete = 1;
  }

  if (impl.includeTxPower) {
    fields.tx_pwr_lvl = BLE.getPower();
    fields.tx_pwr_lvl_is_present = 1;
  }

  if (impl.appearance > 0) {
    fields.appearance = impl.appearance;
    fields.appearance_is_present = 1;
  }

  if (impl.useCustomAdvData) {
    int rc = ble_gap_adv_set_data(impl.customAdvData.data(), impl.customAdvData.length());
    if (rc != 0) {
      log_e("ble_gap_adv_set_data: rc=%d", rc);
      return BTStatus::Fail;
    }
  } else {
    // Build service UUIDs into fields (16, 32, and 128-bit)
    ble_uuid16_t uuid16s[10];
    ble_uuid32_t uuid32s[10];
    ble_uuid128_t uuid128s[4];
    int n16 = 0, n32 = 0, n128 = 0;
    for (auto &uuid : impl.serviceUUIDs) {
      switch (uuid.bitSize()) {
        case 16:
          if (n16 < 10) {
            uuid16s[n16].u.type = BLE_UUID_TYPE_16;
            uuid16s[n16].value = uuid.toUint16();
            n16++;
          }
          break;
        case 32:
          if (n32 < 10) {
            uuid32s[n32].u.type = BLE_UUID_TYPE_32;
            uuid32s[n32].value = uuid.toUint32();
            n32++;
          }
          break;
        default: {
          if (n128 < 4) {
            BLEUUID u128 = uuid.to128();
            uuid128s[n128].u.type = BLE_UUID_TYPE_128;
            const uint8_t *be = u128.data();
            for (int i = 0; i < 16; i++) {
              uuid128s[n128].value[15 - i] = be[i];
            }
            n128++;
          }
          break;
        }
      }
    }
    if (n16 > 0) {
      fields.uuids16 = uuid16s;
      fields.num_uuids16 = n16;
      fields.uuids16_is_complete = 1;
    }
    if (n32 > 0) {
      fields.uuids32 = uuid32s;
      fields.num_uuids32 = n32;
      fields.uuids32_is_complete = 1;
    }
    if (n128 > 0) {
      fields.uuids128 = uuid128s;
      fields.num_uuids128 = n128;
      fields.uuids128_is_complete = 1;
    }

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
      log_e("ble_gap_adv_set_fields: rc=%d", rc);
      return BTStatus::Fail;
    }
  }

  if (impl.scanResponseEnabled && impl.useCustomScanRsp) {
    int rc = ble_gap_adv_rsp_set_data(impl.customScanRspData.data(), impl.customScanRspData.length());
    if (rc != 0) {
      log_e("ble_gap_adv_rsp_set_data: rc=%d", rc);
    }
  }

  int32_t duration = (durationMs == 0) ? BLE_HS_FOREVER : (durationMs / 10);
  int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, duration, &advParams, Impl::gapEventCallback, &impl);
  if (rc != 0) {
    log_e("ble_gap_adv_start: rc=%d", rc);
    return BTStatus::Fail;
  }

  log_i("Advertising: started (duration=%u ms, %u service UUID(s))", durationMs, (unsigned)impl.serviceUUIDs.size());
  impl.advertising = true;
  return BTStatus::OK;
}

BTStatus BLEAdvertising::stop() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (!impl.advertising) return BTStatus::OK;
  log_d("Advertising: stop");
  int rc = ble_gap_adv_stop();
  impl.advertising = false;
  if (rc == 0) {
    log_i("Advertising: stopped");
  } else {
    log_e("Advertising: ble_gap_adv_stop rc=%d", rc);
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

void BLEAdvertising::onComplete(CompleteHandler handler) {
  BLE_CHECK_IMPL();
  impl.onCompleteCb = handler;
}

// --------------------------------------------------------------------------
// BLEClass::getAdvertising() + advertising shortcuts
// --------------------------------------------------------------------------

BLEAdvertising BLEClass::getAdvertising() {
  if (!isInitialized()) {
    log_e("getAdvertising: BLE not initialized");
    return BLEAdvertising();
  }
  static std::shared_ptr<BLEAdvertising::Impl> advImpl;
  if (!advImpl) {
    advImpl = std::make_shared<BLEAdvertising::Impl>();
  }
  return BLEAdvertising(advImpl);
}

BTStatus BLEClass::startAdvertising() {
  return getAdvertising().start();
}

BTStatus BLEClass::stopAdvertising() {
  return getAdvertising().stop();
}

#endif /* BLE_NIMBLE */
