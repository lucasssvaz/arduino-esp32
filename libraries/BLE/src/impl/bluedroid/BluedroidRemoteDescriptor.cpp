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
#include "BluedroidClient.h"
#include "BluedroidRemoteTypes.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <esp_gattc_api.h>
#include <string.h>

// --------------------------------------------------------------------------
// BLERemoteDescriptor -- Bluedroid backend
// --------------------------------------------------------------------------

// --------------------------------------------------------------------------
// Read
// --------------------------------------------------------------------------

String BLERemoteDescriptor::readValue(uint32_t timeoutMs) {
  if (!_impl || !_impl->chr || !_impl->chr->service || !_impl->chr->service->client) return "";
  auto *client = _impl->chr->service->client;
  if (!client->connected) return "";

  client->readBuf.clear();
  client->readSync.take();

  esp_err_t err = esp_ble_gattc_read_char_descr(
    client->gattcIf, client->connId,
    _impl->handle, ESP_GATT_AUTH_REQ_NONE);

  if (err != ESP_OK) {
    log_e("esp_ble_gattc_read_char_descr: %s", esp_err_to_name(err));
    client->readSync.give(BTStatus::Fail);
    return "";
  }

  BTStatus st = client->readSync.wait(timeoutMs);
  if (st != BTStatus::OK) return "";

  _impl->value = client->readBuf;
  return String(reinterpret_cast<const char *>(client->readBuf.data()), client->readBuf.size());
}

// --------------------------------------------------------------------------
// Write
// --------------------------------------------------------------------------

BTStatus BLERemoteDescriptor::writeValue(const uint8_t *data, size_t len, bool withResponse) {
  if (!_impl || !_impl->chr || !_impl->chr->service || !_impl->chr->service->client) return BTStatus::InvalidState;
  auto *client = _impl->chr->service->client;
  if (!client->connected) return BTStatus::NotConnected;

  esp_gatt_write_type_t writeType = withResponse
    ? ESP_GATT_WRITE_TYPE_RSP
    : ESP_GATT_WRITE_TYPE_NO_RSP;

  if (withResponse) {
    client->writeSync.take();
  }

  esp_err_t err = esp_ble_gattc_write_char_descr(
    client->gattcIf, client->connId,
    _impl->handle, len,
    const_cast<uint8_t *>(data),
    writeType, ESP_GATT_AUTH_REQ_NONE);

  if (err != ESP_OK) {
    log_e("esp_ble_gattc_write_char_descr: %s", esp_err_to_name(err));
    if (withResponse) client->writeSync.give(BTStatus::Fail);
    return BTStatus::Fail;
  }

  if (withResponse) {
    return client->writeSync.wait(5000);
  }
  return BTStatus::OK;
}

#endif /* BLE_BLUEDROID */
