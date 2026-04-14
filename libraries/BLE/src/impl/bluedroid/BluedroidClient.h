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

#include "BLEClient.h"
#include "impl/BLESync.h"

#include <esp_gattc_api.h>
#include "impl/BLEMutex.h"

struct BLEClient::Impl {
  uint16_t connId = 0xFFFF;
  BTAddress peerAddress;
  bool connected = false;
  esp_gatt_if_t gattcIf = ESP_GATT_IF_NONE;
  BLESync connectSync;
  BLEMutex mtx;

  BLEClient::ConnectHandler onConnectCb = nullptr;
  BLEClient::DisconnectHandler onDisconnectCb = nullptr;
  BLEClient::ConnectFailHandler onConnectFailCb = nullptr;
  BLEClient::MtuChangedHandler onMtuChangedCb = nullptr;
  BLEClient::ConnParamsReqHandler onConnParamsReqCb = nullptr;
  BLEClient::IdentityHandler onIdentityCb = nullptr;
  BLEClient::Callbacks *callbacks = nullptr;
};

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
