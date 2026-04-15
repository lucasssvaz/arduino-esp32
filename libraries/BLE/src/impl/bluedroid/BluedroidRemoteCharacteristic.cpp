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
#include <esp_gatt_defs.h>
#include <string.h>

// Local UUID conversion (same as BluedroidClient.cpp / BluedroidRemoteService.cpp)
static BLEUUID espToUuid(const esp_bt_uuid_t &espUuid) {
  if (espUuid.len == ESP_UUID_LEN_16) {
    return BLEUUID(espUuid.uuid.uuid16);
  } else if (espUuid.len == ESP_UUID_LEN_32) {
    return BLEUUID(espUuid.uuid.uuid32);
  } else {
    return BLEUUID(espUuid.uuid.uuid128, 16, true);
  }
}

// --------------------------------------------------------------------------
// BLERemoteCharacteristic -- Bluedroid backend
// --------------------------------------------------------------------------

BLERemoteCharacteristic::BLERemoteCharacteristic() : _impl(nullptr) {}
BLERemoteCharacteristic::operator bool() const { return _impl != nullptr; }

BLEUUID BLERemoteCharacteristic::getUUID() const {
  return _impl ? _impl->uuid : BLEUUID();
}

uint16_t BLERemoteCharacteristic::getHandle() const {
  return _impl ? _impl->handle : 0;
}

bool BLERemoteCharacteristic::canRead() const {
  return _impl && (_impl->properties & ESP_GATT_CHAR_PROP_BIT_READ);
}

bool BLERemoteCharacteristic::canWrite() const {
  return _impl && (_impl->properties & ESP_GATT_CHAR_PROP_BIT_WRITE);
}

bool BLERemoteCharacteristic::canWriteNoResponse() const {
  return _impl && (_impl->properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR);
}

bool BLERemoteCharacteristic::canNotify() const {
  return _impl && (_impl->properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY);
}

bool BLERemoteCharacteristic::canIndicate() const {
  return _impl && (_impl->properties & ESP_GATT_CHAR_PROP_BIT_INDICATE);
}

bool BLERemoteCharacteristic::canBroadcast() const {
  return _impl && (_impl->properties & ESP_GATT_CHAR_PROP_BIT_BROADCAST);
}

// --------------------------------------------------------------------------
// Read
// --------------------------------------------------------------------------

String BLERemoteCharacteristic::readValue(uint32_t timeoutMs) {
  if (!_impl || !_impl->service || !_impl->service->client) return "";
  auto *client = _impl->service->client;
  if (!client->connected) return "";

  client->readBuf.clear();
  client->readSync.take();

  esp_err_t err = esp_ble_gattc_read_char(
    client->gattcIf, client->connId,
    _impl->handle, ESP_GATT_AUTH_REQ_NONE);

  if (err != ESP_OK) {
    log_e("esp_ble_gattc_read_char: %s", esp_err_to_name(err));
    client->readSync.give(BTStatus::Fail);
    return "";
  }

  BTStatus st = client->readSync.wait(timeoutMs);
  if (st != BTStatus::OK) return "";

  _impl->value = client->readBuf;
  return String(reinterpret_cast<const char *>(client->readBuf.data()), client->readBuf.size());
}

uint8_t BLERemoteCharacteristic::readUInt8(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  return v.length() >= 1 ? static_cast<uint8_t>(v[0]) : 0;
}

uint16_t BLERemoteCharacteristic::readUInt16(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  if (v.length() < 2) return 0;
  return static_cast<uint16_t>(static_cast<uint8_t>(v[0])) |
         (static_cast<uint16_t>(static_cast<uint8_t>(v[1])) << 8);
}

uint32_t BLERemoteCharacteristic::readUInt32(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  if (v.length() < 4) return 0;
  return static_cast<uint32_t>(static_cast<uint8_t>(v[0])) |
         (static_cast<uint32_t>(static_cast<uint8_t>(v[1])) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(v[2])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(v[3])) << 24);
}

float BLERemoteCharacteristic::readFloat(uint32_t timeoutMs) {
  uint32_t raw = readUInt32(timeoutMs);
  float f;
  memcpy(&f, &raw, sizeof(f));
  return f;
}

size_t BLERemoteCharacteristic::readValue(uint8_t *buf, size_t bufLen, uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  size_t copyLen = (v.length() < bufLen) ? v.length() : bufLen;
  memcpy(buf, v.c_str(), copyLen);
  return copyLen;
}

const uint8_t *BLERemoteCharacteristic::readRawData(size_t *len) {
  if (!_impl || _impl->value.empty()) {
    if (len) *len = 0;
    return nullptr;
  }
  if (len) *len = _impl->value.size();
  return _impl->value.data();
}

// --------------------------------------------------------------------------
// Write
// --------------------------------------------------------------------------

BTStatus BLERemoteCharacteristic::writeValue(const uint8_t *data, size_t len, bool withResponse) {
  if (!_impl || !_impl->service || !_impl->service->client) return BTStatus::InvalidState;
  auto *client = _impl->service->client;
  if (!client->connected) return BTStatus::NotConnected;

  esp_gatt_write_type_t writeType = withResponse
    ? ESP_GATT_WRITE_TYPE_RSP
    : ESP_GATT_WRITE_TYPE_NO_RSP;

  if (withResponse) {
    client->writeSync.take();
  }

  esp_err_t err = esp_ble_gattc_write_char(
    client->gattcIf, client->connId,
    _impl->handle, len,
    const_cast<uint8_t *>(data),
    writeType, ESP_GATT_AUTH_REQ_NONE);

  if (err != ESP_OK) {
    log_e("esp_ble_gattc_write_char: %s", esp_err_to_name(err));
    if (withResponse) client->writeSync.give(BTStatus::Fail);
    return BTStatus::Fail;
  }

  if (withResponse) {
    return client->writeSync.wait(10000);
  }
  return BTStatus::OK;
}

BTStatus BLERemoteCharacteristic::writeValue(const String &value, bool withResponse) {
  return writeValue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length(), withResponse);
}

BTStatus BLERemoteCharacteristic::writeValue(uint8_t value, bool withResponse) {
  return writeValue(&value, 1, withResponse);
}

// --------------------------------------------------------------------------
// Subscribe / Unsubscribe
// --------------------------------------------------------------------------

BTStatus BLERemoteCharacteristic::subscribe(bool notifications, NotifyCallback callback) {
  if (!_impl || !_impl->service || !_impl->service->client) return BTStatus::InvalidState;
  auto *client = _impl->service->client;
  if (!client->connected) return BTStatus::NotConnected;

  _impl->notifyCb = callback;

  // Register for notifications/indications with the Bluedroid stack
  esp_bd_addr_t bda;
  memcpy(bda, client->peerAddress.data(), 6);
  esp_err_t err = esp_ble_gattc_register_for_notify(
    client->gattcIf, bda, _impl->handle);
  if (err != ESP_OK) {
    log_e("esp_ble_gattc_register_for_notify: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  // Write CCCD descriptor to enable notifications/indications
  uint16_t cccdVal = notifications ? 0x0001 : 0x0002;

  // Find CCCD descriptor (0x2902) - discover descriptors if needed
  BLERemoteDescriptor cccd = getDescriptor(BLEUUID(static_cast<uint16_t>(0x2902)));
  if (cccd) {
    return cccd.writeValue(reinterpret_cast<const uint8_t *>(&cccdVal), sizeof(cccdVal), true);
  }

  // Fallback: write CCCD at handle+1 (common convention)
  client->writeSync.take();
  err = esp_ble_gattc_write_char_descr(
    client->gattcIf, client->connId,
    _impl->handle + 1, sizeof(cccdVal),
    reinterpret_cast<uint8_t *>(&cccdVal),
    ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    log_e("esp_ble_gattc_write_char_descr (CCCD): %s", esp_err_to_name(err));
    client->writeSync.give(BTStatus::Fail);
    return BTStatus::Fail;
  }
  return client->writeSync.wait(5000);
}

BTStatus BLERemoteCharacteristic::unsubscribe() {
  if (!_impl || !_impl->service || !_impl->service->client) return BTStatus::InvalidState;
  auto *client = _impl->service->client;
  if (!client->connected) return BTStatus::NotConnected;

  _impl->notifyCb = nullptr;

  // Unregister for notifications
  esp_bd_addr_t bda;
  memcpy(bda, client->peerAddress.data(), 6);
  esp_ble_gattc_unregister_for_notify(client->gattcIf, bda, _impl->handle);

  // Write 0x0000 to CCCD
  uint16_t cccdVal = 0x0000;

  BLERemoteDescriptor cccd = getDescriptor(BLEUUID(static_cast<uint16_t>(0x2902)));
  if (cccd) {
    return cccd.writeValue(reinterpret_cast<const uint8_t *>(&cccdVal), sizeof(cccdVal), true);
  }

  // Fallback: handle+1
  client->writeSync.take();
  esp_err_t err = esp_ble_gattc_write_char_descr(
    client->gattcIf, client->connId,
    _impl->handle + 1, sizeof(cccdVal),
    reinterpret_cast<uint8_t *>(&cccdVal),
    ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    client->writeSync.give(BTStatus::Fail);
    return BTStatus::Fail;
  }
  return client->writeSync.wait(5000);
}

// --------------------------------------------------------------------------
// Descriptor discovery
// --------------------------------------------------------------------------

BLERemoteDescriptor BLERemoteCharacteristic::getDescriptor(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLERemoteDescriptor());

  if (!impl.descriptorsRetrieved && impl.service && impl.service->client && impl.service->client->connected) {
    auto *client = impl.service->client;
    uint16_t count = 0;
    esp_gatt_status_t st = esp_ble_gattc_get_attr_count(
      client->gattcIf, client->connId,
      ESP_GATT_DB_DESCRIPTOR, 0, 0, impl.handle, &count);

    if (st == ESP_GATT_OK && count > 0) {
      std::vector<esp_gattc_descr_elem_t> descs(count);
      st = esp_ble_gattc_get_all_descr(
        client->gattcIf, client->connId,
        impl.handle, descs.data(), &count, 0);

      if (st == ESP_GATT_OK) {
        for (uint16_t i = 0; i < count; i++) {
          auto dImpl = std::make_shared<BLERemoteDescriptor::Impl>();
          dImpl->uuid = espToUuid(descs[i].uuid);
          dImpl->handle = descs[i].handle;
          dImpl->chr = _impl.get();
          impl.descriptors.push_back(dImpl);
        }
      }
    }
    impl.descriptorsRetrieved = true;
  }

  BLEUUID target = uuid.to128();
  for (auto &d : impl.descriptors) {
    if (d->uuid.to128() == target) {
      return BLERemoteDescriptor(d);
    }
  }
  return BLERemoteDescriptor();
}

std::vector<BLERemoteDescriptor> BLERemoteCharacteristic::getDescriptors() const {
  std::vector<BLERemoteDescriptor> result;
  if (!_impl) return result;

  // Trigger discovery through non-const cast (lazy init)
  if (!_impl->descriptorsRetrieved && _impl->service && _impl->service->client && _impl->service->client->connected) {
    auto *self = const_cast<BLERemoteCharacteristic *>(this);
    self->getDescriptor(BLEUUID(static_cast<uint16_t>(0)));  // triggers discovery
  }

  for (auto &d : _impl->descriptors) {
    result.push_back(BLERemoteDescriptor(d));
  }
  return result;
}

BLERemoteService BLERemoteCharacteristic::getRemoteService() const {
  return (_impl && _impl->service)
    ? BLERemoteService(std::shared_ptr<BLERemoteService::Impl>(_impl->service, [](BLERemoteService::Impl *){}))
    : BLERemoteService();
}

String BLERemoteCharacteristic::toString() const {
  BLE_CHECK_IMPL("BLERemoteCharacteristic(empty)");
  return "BLERemoteCharacteristic(uuid=" + impl.uuid.toString() + ")";
}

#endif /* BLE_BLUEDROID */
