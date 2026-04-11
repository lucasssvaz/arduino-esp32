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

#include "BLEServer.h"
#include "BLECharacteristic.h"
#include <host/ble_gatt.h>
#include <vector>
#include <memory>

struct BLEService::Impl {
  BLEUUID uuid;
  uint16_t handle = 0;
  uint8_t instId = 0;
  uint32_t numHandles = 15;
  bool started = false;
  std::weak_ptr<BLEServer::Impl> serverImpl;
  std::vector<std::shared_ptr<BLECharacteristic::Impl>> characteristics;
  ble_uuid_any_t nimbleUUID{};
};

// Builds and registers user GATT services with NimBLE.
// Called from BLEServer::start() before ble_gatts_start().
// Defined in NimBLECharacteristic.cpp.
int nimbleRegisterGattServices(
    const std::vector<std::shared_ptr<BLEService::Impl>> &services);

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
