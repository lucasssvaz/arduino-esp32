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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if (defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)) && defined(CONFIG_NIMBLE_ENABLED)

#include "NimBLERemoteTypes.h"
#include "esp32-hal-log.h"
#include "impl/BLEImplHelpers.h"

// ==========================================================================
// BLERemoteDescriptor implementation
// ==========================================================================

BLERemoteDescriptor::BLERemoteDescriptor() : _impl(nullptr) {}
BLERemoteDescriptor::operator bool() const { return _impl != nullptr; }

BLEUUID BLERemoteDescriptor::getUUID() const {
  return _impl ? _impl->uuid : BLEUUID();
}

uint16_t BLERemoteDescriptor::getHandle() const {
  return _impl ? _impl->handle : 0;
}

BLERemoteCharacteristic BLERemoteDescriptor::getRemoteCharacteristic() const {
  return _impl ? BLERemoteCharacteristic(_impl->chrImpl.lock()) : BLERemoteCharacteristic();
}

String BLERemoteDescriptor::readValue(uint32_t timeoutMs) {
  if (!_impl || !isGattConnected(_impl->connHandle)) return "";

  _impl->lastValue.clear();
  _impl->readSync.take();

  int rc = ble_gattc_read(_impl->connHandle, _impl->handle, Impl::readCb, _impl.get());
  if (rc != 0) {
    _impl->readSync.give(BTStatus::Fail);
    return "";
  }

  if (_impl->readSync.wait(timeoutMs) != BTStatus::OK) return "";
  return String(reinterpret_cast<const char *>(_impl->lastValue.data()), _impl->lastValue.size());
}

uint8_t BLERemoteDescriptor::readUInt8(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  return v.length() >= 1 ? static_cast<uint8_t>(v[0]) : 0;
}

uint16_t BLERemoteDescriptor::readUInt16(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  if (v.length() < 2) return 0;
  return static_cast<uint16_t>(static_cast<uint8_t>(v[0])) | (static_cast<uint16_t>(static_cast<uint8_t>(v[1])) << 8);
}

uint32_t BLERemoteDescriptor::readUInt32(uint32_t timeoutMs) {
  String v = readValue(timeoutMs);
  if (v.length() < 4) return 0;
  return static_cast<uint32_t>(static_cast<uint8_t>(v[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(v[1])) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(v[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(v[3])) << 24);
}

BTStatus BLERemoteDescriptor::writeValue(const uint8_t *data, size_t len, bool withResponse) {
  if (!_impl || !isGattConnected(_impl->connHandle)) return BTStatus::InvalidState;

  if (!withResponse) {
    int rc = ble_gattc_write_no_rsp_flat(_impl->connHandle, _impl->handle, data, len);
    return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
  }

  _impl->writeSync.take();
  int rc = ble_gattc_write_flat(_impl->connHandle, _impl->handle, data, len, Impl::writeCb, _impl.get());
  if (rc != 0) {
    _impl->writeSync.give(BTStatus::Fail);
    return BTStatus::Fail;
  }
  return _impl->writeSync.wait(5000);
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

int BLERemoteDescriptor::Impl::readCb(uint16_t connHandle, const struct ble_gatt_error *error,
                                       struct ble_gatt_attr *attr, void *arg) {
  auto *impl = static_cast<BLERemoteDescriptor::Impl *>(arg);
  if (!impl) return 0;

  if (error->status == 0 && attr != nullptr) {
    uint16_t len = OS_MBUF_PKTLEN(attr->om);
    impl->lastValue.resize(len);
    os_mbuf_copydata(attr->om, 0, len, impl->lastValue.data());
    impl->readSync.give(BTStatus::OK);
  } else {
    impl->readSync.give((error->status == BLE_HS_EDONE) ? BTStatus::OK : BTStatus::Fail);
  }
  return 0;
}

int BLERemoteDescriptor::Impl::writeCb(uint16_t connHandle, const struct ble_gatt_error *error,
                                        struct ble_gatt_attr *attr, void *arg) {
  auto *impl = static_cast<BLERemoteDescriptor::Impl *>(arg);
  if (!impl) return 0;
  impl->writeSync.give((error->status == 0) ? BTStatus::OK : BTStatus::Fail);
  return 0;
}

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
