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

#include "BluedroidCharacteristic.h"
#include "BluedroidService.h"
#include "BluedroidServer.h"
#include "impl/BLEImplHelpers.h"
#include "impl/BLEConnInfoData.h"
#include "esp32-hal-log.h"

#include <esp_gatts_api.h>
#include <vector>

// --------------------------------------------------------------------------
// BLECharacteristic -- Bluedroid
// --------------------------------------------------------------------------

BTStatus BLECharacteristic::notify(const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (!(impl.properties & BLEProperty::Notify)) {
    log_w("Characteristic %s: notify called but Notify property not set", impl.uuid.toString().c_str());
    return BTStatus::InvalidState;
  }
  if (!impl.service || !impl.service->server) {
    log_e("Characteristic %s: notify called but service/server not set", impl.uuid.toString().c_str());
    return BTStatus::InvalidState;
  }
  auto *server = impl.service->server;

  const uint8_t *sendData = data;
  size_t sendLen = length;
  if (!sendData || sendLen == 0) {
    sendData = impl.value.data();
    sendLen = impl.value.size();
  }

  BLELockGuard lock(server->mtx);
  for (auto &sub : impl.subscribers) {
    if (sub.second & 0x0001) {
      esp_ble_gatts_send_indicate(server->gattsIf, sub.first, impl.handle,
                                  sendLen, const_cast<uint8_t *>(sendData), false);
    }
  }
  return BTStatus::OK;
}

BTStatus BLECharacteristic::notify(uint16_t connHandle, const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (!(impl.properties & BLEProperty::Notify)) {
    log_w("Characteristic %s: notify(conn) called but Notify property not set", impl.uuid.toString().c_str());
    return BTStatus::InvalidState;
  }
  if (!impl.service || !impl.service->server) {
    log_e("Characteristic %s: notify(conn) called but service/server not set", impl.uuid.toString().c_str());
    return BTStatus::InvalidState;
  }
  auto *server = impl.service->server;

  const uint8_t *sendData = data;
  size_t sendLen = length;
  if (!sendData || sendLen == 0) {
    sendData = impl.value.data();
    sendLen = impl.value.size();
  }

  esp_err_t err = esp_ble_gatts_send_indicate(server->gattsIf, connHandle, impl.handle,
                                               sendLen, const_cast<uint8_t *>(sendData), false);
  if (err != ESP_OK) {
    log_e("Characteristic %s: esp_ble_gatts_send_indicate (notify) conn=%u: %s",
          impl.uuid.toString().c_str(), connHandle, esp_err_to_name(err));
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

BTStatus BLECharacteristic::indicate(const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (!(impl.properties & BLEProperty::Indicate)) {
    log_w("Characteristic %s: indicate called but Indicate property not set", impl.uuid.toString().c_str());
    return BTStatus::InvalidState;
  }
  if (!impl.service || !impl.service->server) {
    log_e("Characteristic %s: indicate called but service/server not set", impl.uuid.toString().c_str());
    return BTStatus::InvalidState;
  }
  auto *server = impl.service->server;

  const uint8_t *sendData = data;
  size_t sendLen = length;
  if (!sendData || sendLen == 0) {
    sendData = impl.value.data();
    sendLen = impl.value.size();
  }

  BLELockGuard lock(server->mtx);
  for (auto &sub : impl.subscribers) {
    if (sub.second & 0x0002) {
      esp_ble_gatts_send_indicate(server->gattsIf, sub.first, impl.handle,
                                  sendLen, const_cast<uint8_t *>(sendData), true);
    }
  }
  return BTStatus::OK;
}

BTStatus BLECharacteristic::indicate(uint16_t connHandle, const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (!(impl.properties & BLEProperty::Indicate)) {
    log_w("Characteristic %s: indicate(conn) called but Indicate property not set", impl.uuid.toString().c_str());
    return BTStatus::InvalidState;
  }
  if (!impl.service || !impl.service->server) {
    log_e("Characteristic %s: indicate(conn) called but service/server not set", impl.uuid.toString().c_str());
    return BTStatus::InvalidState;
  }
  auto *server = impl.service->server;

  const uint8_t *sendData = data;
  size_t sendLen = length;
  if (!sendData || sendLen == 0) {
    sendData = impl.value.data();
    sendLen = impl.value.size();
  }

  esp_err_t err = esp_ble_gatts_send_indicate(server->gattsIf, connHandle, impl.handle,
                                               sendLen, const_cast<uint8_t *>(sendData), true);
  if (err != ESP_OK) {
    log_e("Characteristic %s: esp_ble_gatts_send_indicate (indicate) conn=%u: %s",
          impl.uuid.toString().c_str(), connHandle, esp_err_to_name(err));
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

BLEDescriptor BLECharacteristic::createDescriptor(const BLEUUID &uuid, BLEPermission perms, size_t maxLen) {
  BLE_CHECK_IMPL(BLEDescriptor());
  auto desc = std::make_shared<BLEDescriptor::Impl>();
  desc->uuid = uuid;
  desc->chr = _impl.get();
  desc->value.reserve(maxLen);
  impl.descriptors.push_back(desc);
  return BLEDescriptor(desc);
}

void BLECharacteristic::setDescription(const String &) {}

#endif /* BLE_BLUEDROID */
