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

#pragma once

#include "BLESecurity.h"
#include "impl/BLESync.h"

#if (defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)) && defined(CONFIG_NIMBLE_ENABLED)

#include <host/ble_hs.h>

struct BLESecurity::Impl {
  BLESync authSync;
  IOCapability ioCap = NoInputNoOutput;
  bool bonding = false;
  bool mitm = false;
  bool sc = true;
  bool forceAuth = false;
  bool staticPassKey = false;
  bool regenOnConnect = false;
  uint32_t passKey = 0;
  uint8_t keySize = 16;
  uint8_t initKeyDist = BLE_SM_PAIR_KEY_DIST_ENC;
  uint8_t respKeyDist = BLE_SM_PAIR_KEY_DIST_ENC;

  PassKeyRequestHandler passKeyRequestCb;
  PassKeyDisplayHandler passKeyDisplayCb;
  ConfirmPassKeyHandler confirmPassKeyCb;
  SecurityRequestHandler securityRequestCb;
  AuthorizationHandler authorizationCb;
  AuthCompleteHandler authCompleteCb;
  BondStoreOverflowHandler bondOverflowCb;

  void applyToHost() const {
    ble_hs_cfg.sm_io_cap = static_cast<uint8_t>(ioCap);
    ble_hs_cfg.sm_bonding = bonding ? 1 : 0;
    ble_hs_cfg.sm_mitm = mitm ? 1 : 0;
    ble_hs_cfg.sm_sc = sc ? 1 : 0;
    ble_hs_cfg.sm_our_key_dist = initKeyDist;
    ble_hs_cfg.sm_their_key_dist = respKeyDist;
  }
};

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
