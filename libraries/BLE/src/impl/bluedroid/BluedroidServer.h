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
#if defined(SOC_BLE_SUPPORTED) && defined(CONFIG_BLUEDROID_ENABLED)

#include "BLEServer.h"
#include "impl/BLESync.h"

#include <esp_gatts_api.h>
#include "impl/BLEMutex.h"
#include <vector>

struct BLEServer::Impl {
  bool started = false;
  bool advertiseOnDisconnect = true;
  esp_gatt_if_t gattsIf = ESP_GATT_IF_NONE;
  uint16_t appId = 0;

  std::vector<std::shared_ptr<BLEService::Impl>> services;

  BLEServer::ConnectHandler onConnectCb = nullptr;
  BLEServer::DisconnectHandler onDisconnectCb = nullptr;
  BLEServer::MtuChangedHandler onMtuChangedCb = nullptr;
  BLEServer::ConnParamsHandler onConnParamsCb = nullptr;
  BLEServer::IdentityHandler onIdentityCb = nullptr;
  BLEServer::Callbacks *callbacks = nullptr;

  std::vector<std::pair<uint16_t, BLEConnInfo>> connections;
  BLEMutex mtx;
  BLESync regSync;

  void connSet(uint16_t connHandle, const BLEConnInfo &connInfo);
  void connErase(uint16_t connHandle);
};

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
