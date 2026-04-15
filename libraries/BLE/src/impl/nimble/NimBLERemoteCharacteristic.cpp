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

#include "impl/BLEGuards.h"
#if BLE_NIMBLE

#include "BLE.h"
#include "NimBLERemoteTypes.h"
#include "NimBLEUUID.h"
#include "esp32-hal-log.h"
#include "impl/BLEImplHelpers.h"

#include <host/ble_gap.h>
#include "impl/BLEMutex.h"
#include <utility>
#include <vector>

struct NotifyEntry {
  uint16_t connHandle;
  uint16_t attrHandle;
  std::shared_ptr<BLERemoteCharacteristic::Impl> chr;
};

static SemaphoreHandle_t sNotifyMtx = xSemaphoreCreateRecursiveMutex();
static std::vector<NotifyEntry> sNotifyRegistry;

static bool isAuthError(int rc) {
  return rc == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) ||
         rc == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC) ||
         rc == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHOR);
}

static bool initiateSecurityAndWait(uint16_t connHandle) {
  int rc = ble_gap_security_initiate(connHandle);
  if (rc != 0) return false;
  BLESecurity sec = BLE.getSecurity();
  if (!sec) return false;
  return sec.waitForAuthenticationComplete(10000);
}

// ==========================================================================
// BLERemoteCharacteristic implementation
// ==========================================================================

uint16_t BLERemoteCharacteristic::getHandle() const {
  return _impl ? _impl->valHandle : 0;
}

bool BLERemoteCharacteristic::canRead() const { return _impl && (_impl->properties & BLE_GATT_CHR_PROP_READ); }
bool BLERemoteCharacteristic::canWrite() const { return _impl && (_impl->properties & BLE_GATT_CHR_PROP_WRITE); }
bool BLERemoteCharacteristic::canWriteNoResponse() const { return _impl && (_impl->properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP); }
bool BLERemoteCharacteristic::canNotify() const { return _impl && (_impl->properties & BLE_GATT_CHR_PROP_NOTIFY); }
bool BLERemoteCharacteristic::canIndicate() const { return _impl && (_impl->properties & BLE_GATT_CHR_PROP_INDICATE); }
bool BLERemoteCharacteristic::canBroadcast() const { return _impl && (_impl->properties & BLE_GATT_CHR_PROP_BROADCAST); }

String BLERemoteCharacteristic::readValue(uint32_t timeoutMs) {
  if (!_impl || !isGattConnected(_impl->connHandle)) return "";

  for (int retry = 0; retry < 2; retry++) {
    _impl->lastValue.clear();
    _impl->lastReadRC = 0;
    _impl->readSync.take();

    int rc = ble_gattc_read_long(_impl->connHandle, _impl->valHandle, 0, Impl::readCb, _impl.get());
    if (rc != 0) {
      _impl->readSync.give(BTStatus::Fail);
      return "";
    }

    BTStatus status = _impl->readSync.wait(timeoutMs);

    if (status != BTStatus::OK && _impl->lastReadRC == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_LONG)) {
      _impl->lastValue.clear();
      _impl->lastReadRC = 0;
      _impl->readSync.take();
      rc = ble_gattc_read(_impl->connHandle, _impl->valHandle, Impl::readCb, _impl.get());
      if (rc != 0) {
        _impl->readSync.give(BTStatus::Fail);
        return "";
      }
      status = _impl->readSync.wait(timeoutMs);
    }

    if (status == BTStatus::OK) {
      return String(reinterpret_cast<const char *>(_impl->lastValue.data()), _impl->lastValue.size());
    }

    if (retry == 0 && isAuthError(_impl->lastReadRC)) {
      if (!initiateSecurityAndWait(_impl->connHandle)) return "";
      continue;
    }
    break;
  }
  return "";
}

const uint8_t *BLERemoteCharacteristic::readRawData(size_t *len) {
  if (!_impl || _impl->lastValue.empty()) {
    if (len) *len = 0;
    return nullptr;
  }
  if (len) *len = _impl->lastValue.size();
  return _impl->lastValue.data();
}

BTStatus BLERemoteCharacteristic::writeValue(const uint8_t *data, size_t len, bool withResponse) {
  if (!_impl || !isGattConnected(_impl->connHandle)) return BTStatus::InvalidState;

  uint16_t mtu = ble_att_mtu(_impl->connHandle);
  uint16_t maxSingle = (mtu > 3) ? (mtu - 3) : 0;

  if (!withResponse && len <= maxSingle) {
    int rc = ble_gattc_write_no_rsp_flat(_impl->connHandle, _impl->valHandle, data, len);
    return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
  }

  bool isLong = (len > maxSingle);

  for (int retry = 0; retry < 2; retry++) {
    _impl->lastWriteRC = 0;
    _impl->writeSync.take();

    int rc;
    if (isLong) {
      struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
      if (!om) {
        _impl->writeSync.give(BTStatus::NoMemory);
        return BTStatus::NoMemory;
      }
      rc = ble_gattc_write_long(_impl->connHandle, _impl->valHandle, 0, om, Impl::writeCb, _impl.get());
    } else {
      rc = ble_gattc_write_flat(_impl->connHandle, _impl->valHandle, data, len, Impl::writeCb, _impl.get());
    }

    if (rc != 0) {
      _impl->writeSync.give(BTStatus::Fail);
      return BTStatus::Fail;
    }

    BTStatus status = _impl->writeSync.wait(10000);
    if (status == BTStatus::OK) return BTStatus::OK;

    if (retry == 0 && isAuthError(_impl->lastWriteRC)) {
      if (!initiateSecurityAndWait(_impl->connHandle)) return BTStatus::AuthFailed;
      continue;
    }
    return status;
  }
  return BTStatus::Fail;
}

BTStatus BLERemoteCharacteristic::subscribe(bool notifications, NotifyCallback callback) {
  if (!_impl || !isGattConnected(_impl->connHandle)) return BTStatus::InvalidState;

  _impl->notifyCb = callback;

  Impl::registerForNotify(_impl->connHandle, _impl->valHandle, _impl);

  uint16_t cccdVal = notifications ? 0x0001 : 0x0002;
  uint16_t cccdHandle = _impl->valHandle + 1;

  _impl->writeSync.take();
  int rc = ble_gattc_write_flat(_impl->connHandle, cccdHandle, &cccdVal, sizeof(cccdVal), Impl::writeCb, _impl.get());
  if (rc != 0) {
    Impl::unregisterForNotify(_impl->connHandle, _impl->valHandle);
    _impl->writeSync.give(BTStatus::Fail);
    return BTStatus::Fail;
  }

  BTStatus status = _impl->writeSync.wait(5000);
  if (status != BTStatus::OK) {
    Impl::unregisterForNotify(_impl->connHandle, _impl->valHandle);
  }
  return status;
}

BTStatus BLERemoteCharacteristic::unsubscribe() {
  if (!_impl || !isGattConnected(_impl->connHandle)) return BTStatus::InvalidState;

  Impl::unregisterForNotify(_impl->connHandle, _impl->valHandle);
  _impl->notifyCb = nullptr;

  uint16_t cccdVal = 0x0000;
  uint16_t cccdHandle = _impl->valHandle + 1;

  _impl->writeSync.take();
  int rc = ble_gattc_write_flat(_impl->connHandle, cccdHandle, &cccdVal, sizeof(cccdVal), Impl::writeCb, _impl.get());
  if (rc != 0) {
    _impl->writeSync.give(BTStatus::Fail);
    return BTStatus::Fail;
  }
  return _impl->writeSync.wait(5000);
}

BLERemoteDescriptor BLERemoteCharacteristic::getDescriptor(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLERemoteDescriptor());

  if (!impl.descsDiscovered) {
    if (!isGattConnected(impl.connHandle)) return BLERemoteDescriptor();

    auto svc = impl.service;
    uint16_t endHandle = svc ? svc->endHandle : 0xFFFF;

    impl.dscDiscoverSync.take();
    int rc = ble_gattc_disc_all_dscs(impl.connHandle, impl.valHandle, endHandle,
                                     Impl::dscDiscoveryCb, _impl.get());
    if (rc != 0) {
      impl.dscDiscoverSync.give(BTStatus::Fail);
      return BLERemoteDescriptor();
    }
    BTStatus status = impl.dscDiscoverSync.wait(10000);
    if (status == BTStatus::OK) {
      impl.descsDiscovered = true;
      for (auto &d : impl.descriptors) {
        d->connHandle = impl.connHandle;
        d->chr = _impl.get();
      }
    }
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
  BLE_CHECK_IMPL(result);
  for (auto &d : impl.descriptors) {
    result.push_back(BLERemoteDescriptor(d));
  }
  return result;
}

int BLERemoteCharacteristic::Impl::readCb(uint16_t connHandle, const struct ble_gatt_error *error,
                                           struct ble_gatt_attr *attr, void *arg) {
  auto *impl = static_cast<BLERemoteCharacteristic::Impl *>(arg);
  if (!impl) return 0;

  impl->lastReadRC = error->status;

  if (error->status == 0 && attr != nullptr) {
    uint16_t len = OS_MBUF_PKTLEN(attr->om);
    size_t offset = impl->lastValue.size();
    impl->lastValue.resize(offset + len);
    os_mbuf_copydata(attr->om, 0, len, impl->lastValue.data() + offset);
    return 0;
  }

  impl->readSync.give((error->status == BLE_HS_EDONE) ? BTStatus::OK : BTStatus::Fail);
  return 0;
}

int BLERemoteCharacteristic::Impl::writeCb(uint16_t connHandle, const struct ble_gatt_error *error,
                                            struct ble_gatt_attr *attr, void *arg) {
  auto *impl = static_cast<BLERemoteCharacteristic::Impl *>(arg);
  if (!impl) return 0;
  impl->lastWriteRC = error->status;

  // Signal on every callback. For long writes (ble_gattc_write_long), the
  // callback fires for each prepared write and for the execute write, all
  // with status 0 on success.  The first callback wakes the waiter;
  // subsequent ones are harmless no-ops (the BLESync waiter is already
  // cleared after the first give/wait pair completes).
  if (error->status == 0 || error->status == BLE_HS_EDONE) {
    impl->writeSync.give(BTStatus::OK);
  } else {
    impl->writeSync.give(BTStatus::Fail);
  }
  return 0;
}

int BLERemoteCharacteristic::Impl::dscDiscoveryCb(uint16_t connHandle, const struct ble_gatt_error *error,
                                                   uint16_t chrHandle, const struct ble_gatt_dsc *dsc, void *arg) {
  auto *impl = static_cast<BLERemoteCharacteristic::Impl *>(arg);
  if (!impl) return 0;

  if (error->status == 0 && dsc != nullptr) {
    auto dImpl = std::make_shared<BLERemoteDescriptor::Impl>();
    dImpl->uuid = nimbleToUuid(dsc->uuid);
    dImpl->handle = dsc->handle;
    impl->descriptors.push_back(dImpl);
    return 0;
  }

  impl->dscDiscoverSync.give((error->status == BLE_HS_EDONE) ? BTStatus::OK : BTStatus::Fail);
  return 0;
}

int BLERemoteCharacteristic::Impl::notifyCb_static(uint16_t connHandle, const struct ble_gatt_error *error,
                                                    struct ble_gatt_attr *attr, void *arg) {
  auto *impl = static_cast<BLERemoteCharacteristic::Impl *>(arg);
  if (!impl || !attr) return 0;

  uint16_t len = OS_MBUF_PKTLEN(attr->om);
  std::vector<uint8_t> data(len);
  os_mbuf_copydata(attr->om, 0, len, data.data());

  impl->lastValue = data;

  if (impl->notifyCb) {
    BLERemoteCharacteristic chr{std::shared_ptr<BLERemoteCharacteristic::Impl>(impl, [](BLERemoteCharacteristic::Impl *){})};
    impl->notifyCb(chr, data.data(), data.size(), false);
  }
  return 0;
}

void BLERemoteCharacteristic::Impl::registerForNotify(uint16_t connHandle, uint16_t attrHandle,
                                                       const std::shared_ptr<BLERemoteCharacteristic::Impl> &impl) {
  BLELockGuard lock(sNotifyMtx);
  for (auto &entry : sNotifyRegistry) {
    if (entry.connHandle == connHandle && entry.attrHandle == attrHandle) {
      entry.chr = impl;
      return;
    }
  }
  sNotifyRegistry.push_back({connHandle, attrHandle, impl});
}

void BLERemoteCharacteristic::Impl::unregisterForNotify(uint16_t connHandle, uint16_t attrHandle) {
  BLELockGuard lock(sNotifyMtx);
  for (auto it = sNotifyRegistry.begin(); it != sNotifyRegistry.end(); ++it) {
    if (it->connHandle == connHandle && it->attrHandle == attrHandle) {
      sNotifyRegistry.erase(it);
      return;
    }
  }
}

void BLERemoteCharacteristic::Impl::handleNotifyRx(uint16_t connHandle, uint16_t attrHandle,
                                                     struct os_mbuf *om, bool isNotify) {
  BLERemoteCharacteristic::Impl *impl = nullptr;
  {
    BLELockGuard lock(sNotifyMtx);
    for (auto &entry : sNotifyRegistry) {
      if (entry.connHandle == connHandle && entry.attrHandle == attrHandle) {
        impl = entry.chr.get();
        break;
      }
    }
  }
  if (!impl) return;

  uint16_t len = OS_MBUF_PKTLEN(om);
  std::vector<uint8_t> data(len);
  os_mbuf_copydata(om, 0, len, data.data());

  impl->lastValue = data;

  if (impl->notifyCb) {
    BLERemoteCharacteristic chr{std::shared_ptr<BLERemoteCharacteristic::Impl>(impl, [](BLERemoteCharacteristic::Impl *){})};
    impl->notifyCb(chr, data.data(), data.size(), isNotify);
  }
}

#endif /* BLE_NIMBLE */
