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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if (defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)) && defined(CONFIG_NIMBLE_ENABLED)

#include "BLECharacteristic.h"
#include "BLEDescriptor.h"
#include "NimBLEService.h"
#include "NimBLEConnInfo.h"

#include <host/ble_hs.h>
#include <host/ble_gatt.h>
#include <host/ble_att.h>
#include <mutex>
#include <map>
#include <vector>
#include <memory>

void uuidToNimble(const BLEUUID &uuid, ble_uuid_any_t &out);

struct BLEDescriptor::Impl {
  BLEUUID uuid;
  uint16_t handle = 0;
  std::vector<uint8_t> value;
  BLEDescriptor::ReadHandler onReadCb;
  BLEDescriptor::WriteHandler onWriteCb;
  std::weak_ptr<BLECharacteristic::Impl> charImpl;
  uint8_t attFlags = 0;
  ble_uuid_any_t nimbleUUID{};
  std::mutex mtx;
};

struct BLECharacteristic::Impl {
  BLEUUID uuid;
  BLEProperty properties{};
  BLEPermission permissions{};
  uint16_t handle = 0;
  std::vector<uint8_t> value;
  std::mutex valueMtx;

  BLECharacteristic::ReadHandler onReadCb;
  BLECharacteristic::WriteHandler onWriteCb;
  BLECharacteristic::NotifyHandler onNotifyCb;
  BLECharacteristic::SubscribeHandler onSubscribeCb;
  BLECharacteristic::StatusHandler onStatusCb;

  std::vector<std::shared_ptr<BLEDescriptor::Impl>> descriptors;
  std::map<uint16_t, uint16_t> subscribers;

  std::weak_ptr<BLEService::Impl> serviceImpl;
  ble_uuid_any_t nimbleUUID{};

  static int accessCallback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
  static int descAccessCallback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
};

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
