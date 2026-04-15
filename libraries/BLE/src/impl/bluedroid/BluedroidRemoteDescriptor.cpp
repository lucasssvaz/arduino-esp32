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

BLERemoteDescriptor::BLERemoteDescriptor() : _impl(nullptr) {}
BLERemoteDescriptor::operator bool() const { return _impl != nullptr; }

BLEUUID BLERemoteDescriptor::getUUID() const {
  return _impl ? _impl->uuid : BLEUUID();
}

uint16_t BLERemoteDescriptor::getHandle() const {
  return _impl ? _impl->handle : 0;
}

BLERemoteCharacteristic BLERemoteDescriptor::getRemoteCharacteristic() const {
  return (_impl && _impl->chr)
    ? BLERemoteCharacteristic(std::shared_ptr<BLERemoteCharacteristic::Impl>(_impl->chr, [](BLERemoteCharacteristic::Impl *){}))
    : BLERemoteCharacteristic();
}

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

uint8_t BLERemoteDescriptor::readUInt8(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  return v.length() >= 1 ? static_cast<uint8_t>(v[0]) : 0;
}

uint16_t BLERemoteDescriptor::readUInt16(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  if (v.length() < 2) return 0;
  return static_cast<uint16_t>(static_cast<uint8_t>(v[0])) |
         (static_cast<uint16_t>(static_cast<uint8_t>(v[1])) << 8);
}

uint32_t BLERemoteDescriptor::readUInt32(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  if (v.length() < 4) return 0;
  return static_cast<uint32_t>(static_cast<uint8_t>(v[0])) |
         (static_cast<uint32_t>(static_cast<uint8_t>(v[1])) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(v[2])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(v[3])) << 24);
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

BTStatus BLERemoteDescriptor::writeValue(const String &value, bool withResponse) {
  return writeValue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length(), withResponse);
}

BTStatus BLERemoteDescriptor::writeValue(uint8_t value, bool withResponse) {
  return writeValue(&value, 1, withResponse);
}

String BLERemoteDescriptor::toString() const {
  BLE_CHECK_IMPL("BLERemoteDescriptor(empty)");
  return "BLERemoteDescriptor(uuid=" + impl.uuid.toString() + ")";
}

#endif /* BLE_BLUEDROID */
