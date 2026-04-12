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

#include "NimBLECharacteristic.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <algorithm>

// --------------------------------------------------------------------------
// UUID conversion: BLEUUID (big-endian) <-> NimBLE ble_uuid_any_t (LE for 128)
// --------------------------------------------------------------------------

void uuidToNimble(const BLEUUID &uuid, ble_uuid_any_t &out) {
  switch (uuid.bitSize()) {
    case 16: {
      out.u.type = BLE_UUID_TYPE_16;
      out.u16.value = uuid.toUint16();
      break;
    }
    case 32: {
      out.u.type = BLE_UUID_TYPE_32;
      out.u32.value = uuid.toUint32();
      break;
    }
    case 128: {
      out.u.type = BLE_UUID_TYPE_128;
      const uint8_t *be = uuid.data();
      for (int i = 0; i < 16; i++) {
        out.u128.value[15 - i] = be[i];
      }
      break;
    }
    default: {
      BLEUUID u128 = uuid.to128();
      out.u.type = BLE_UUID_TYPE_128;
      const uint8_t *be = u128.data();
      for (int i = 0; i < 16; i++) {
        out.u128.value[15 - i] = be[i];
      }
      break;
    }
  }
}

// --------------------------------------------------------------------------
// GATT access callback for characteristics
// --------------------------------------------------------------------------

int BLECharacteristic::Impl::accessCallback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  auto *impl = static_cast<BLECharacteristic::Impl *>(arg);
  if (!impl) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
      if (conn_handle != BLE_HS_CONN_HANDLE_NONE && impl->onReadCb) {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(conn_handle, &desc) == 0) {
          BLECharacteristic chr(std::shared_ptr<BLECharacteristic::Impl>(std::shared_ptr<void>{}, impl));
          impl->onReadCb(chr, BLEConnInfoImpl::fromDesc(desc));
        }
      }

      std::lock_guard<std::mutex> lock(impl->valueMtx);
      int rc = os_mbuf_append(ctxt->om, impl->value.data(), impl->value.size());
      return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
      uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
      if (len > BLE_ATT_ATTR_MAX_LEN) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      uint8_t buf[BLE_ATT_ATTR_MAX_LEN];
      os_mbuf_copydata(ctxt->om, 0, len, buf);

      {
        std::lock_guard<std::mutex> lock(impl->valueMtx);
        impl->value.assign(buf, buf + len);
      }

      if (impl->onWriteCb) {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(conn_handle, &desc) == 0) {
          BLECharacteristic chr(std::shared_ptr<BLECharacteristic::Impl>(std::shared_ptr<void>{}, impl));
          impl->onWriteCb(chr, BLEConnInfoImpl::fromDesc(desc));
        }
      }
      return 0;
    }

    default: return BLE_ATT_ERR_UNLIKELY;
  }
}

// --------------------------------------------------------------------------
// GATT access callback for descriptors
// --------------------------------------------------------------------------

int BLECharacteristic::Impl::descAccessCallback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  auto *descImpl = static_cast<BLEDescriptor::Impl *>(arg);
  if (!descImpl) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_DSC: {
      if (descImpl->onReadCb && conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(conn_handle, &desc) == 0) {
          BLEDescriptor dsc(std::shared_ptr<BLEDescriptor::Impl>(std::shared_ptr<void>{}, descImpl));
          descImpl->onReadCb(dsc, BLEConnInfoImpl::fromDesc(desc));
        }
      }
      std::lock_guard<std::mutex> lock(descImpl->mtx);
      int rc = os_mbuf_append(ctxt->om, descImpl->value.data(), descImpl->value.size());
      return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    case BLE_GATT_ACCESS_OP_WRITE_DSC: {
      uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
      uint8_t buf[BLE_ATT_ATTR_MAX_LEN];
      if (len > BLE_ATT_ATTR_MAX_LEN) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      os_mbuf_copydata(ctxt->om, 0, len, buf);
      {
        std::lock_guard<std::mutex> lock(descImpl->mtx);
        descImpl->value.assign(buf, buf + len);
      }
      if (descImpl->onWriteCb && conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(conn_handle, &desc) == 0) {
          BLEDescriptor dsc(std::shared_ptr<BLEDescriptor::Impl>(std::shared_ptr<void>{}, descImpl));
          descImpl->onWriteCb(dsc, BLEConnInfoImpl::fromDesc(desc));
        }
      }
      return 0;
    }

    default: return BLE_ATT_ERR_UNLIKELY;
  }
}

// --------------------------------------------------------------------------
// BLECharacteristic::Impl subscriber helpers (flat vector, no std::map)
// --------------------------------------------------------------------------

void BLECharacteristic::Impl::subscriberSet(uint16_t connHandle, uint16_t subVal) {
  for (auto &kv : subscribers) {
    if (kv.first == connHandle) {
      kv.second = subVal;
      return;
    }
  }
  subscribers.emplace_back(connHandle, subVal);
}

void BLECharacteristic::Impl::subscriberErase(uint16_t connHandle) {
  subscribers.erase(
    std::remove_if(subscribers.begin(), subscribers.end(),
      [connHandle](const std::pair<uint16_t, uint16_t> &kv) { return kv.first == connHandle; }),
    subscribers.end());
}

uint16_t BLECharacteristic::Impl::subscriberGet(uint16_t connHandle) const {
  for (const auto &kv : subscribers) {
    if (kv.first == connHandle) {
      return kv.second;
    }
  }
  return 0;
}

// --------------------------------------------------------------------------
// BLECharacteristic public API
// --------------------------------------------------------------------------

BLECharacteristic::BLECharacteristic() : _impl(nullptr) {}

BLECharacteristic::operator bool() const {
  return _impl != nullptr;
}

BTStatus BLECharacteristic::onRead(ReadHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onReadCb.set(std::move(handler));
  return BTStatus::OK;
}

BTStatus BLECharacteristic::onWrite(WriteHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onWriteCb.set(std::move(handler));
  return BTStatus::OK;
}

BTStatus BLECharacteristic::onNotify(NotifyHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onNotifyCb.set(std::move(handler));
  return BTStatus::OK;
}

BTStatus BLECharacteristic::onSubscribe(SubscribeHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onSubscribeCb.set(std::move(handler));
  return BTStatus::OK;
}

BTStatus BLECharacteristic::onStatus(StatusHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.onStatusCb.set(std::move(handler));
  return BTStatus::OK;
}

void BLECharacteristic::setValue(const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL();
  std::lock_guard<std::mutex> lock(impl.valueMtx);
  impl.value.assign(data, data + length);
}

void BLECharacteristic::setValue(const String &value) {
  setValue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
}

void BLECharacteristic::setValue(uint16_t v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }
void BLECharacteristic::setValue(uint32_t v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }
void BLECharacteristic::setValue(int v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }
void BLECharacteristic::setValue(float v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }
void BLECharacteristic::setValue(double v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }

const uint8_t *BLECharacteristic::getValue(size_t *length) const {
  if (!_impl) {
    if (length) *length = 0;
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_impl->valueMtx);
  if (length) *length = _impl->value.size();
  return _impl->value.empty() ? nullptr : _impl->value.data();
}

String BLECharacteristic::getStringValue() const {
  BLE_CHECK_IMPL("");
  std::lock_guard<std::mutex> lock(impl.valueMtx);
  return String(reinterpret_cast<const char *>(impl.value.data()), impl.value.size());
}

BTStatus BLECharacteristic::notify(const uint8_t *data, size_t length) {
  if (!_impl || _impl->handle == 0) return BTStatus::InvalidState;

  for (auto &[connHandle, subVal] : _impl->subscribers) {
    if (subVal & 0x0001) {
      notify(connHandle, data, length);
    }
  }
  return BTStatus::OK;
}

BTStatus BLECharacteristic::notify(uint16_t connHandle, const uint8_t *data, size_t length) {
  if (!_impl || _impl->handle == 0) return BTStatus::InvalidState;

  struct os_mbuf *om = nullptr;
  if (data && length > 0) {
    om = ble_hs_mbuf_from_flat(data, length);
  } else {
    std::lock_guard<std::mutex> lock(_impl->valueMtx);
    om = ble_hs_mbuf_from_flat(_impl->value.data(), _impl->value.size());
  }
  if (!om) return BTStatus::NoMemory;

  int rc = ble_gatts_notify_custom(connHandle, _impl->handle, om);
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
}

BTStatus BLECharacteristic::indicate(const uint8_t *data, size_t length) {
  if (!_impl || _impl->handle == 0) return BTStatus::InvalidState;

  for (auto &[connHandle, subVal] : _impl->subscribers) {
    if (subVal & 0x0002) {
      indicate(connHandle, data, length);
    }
  }
  return BTStatus::OK;
}

BTStatus BLECharacteristic::indicate(uint16_t connHandle, const uint8_t *data, size_t length) {
  if (!_impl || _impl->handle == 0) return BTStatus::InvalidState;

  struct os_mbuf *om = nullptr;
  if (data && length > 0) {
    om = ble_hs_mbuf_from_flat(data, length);
  } else {
    std::lock_guard<std::mutex> lock(_impl->valueMtx);
    om = ble_hs_mbuf_from_flat(_impl->value.data(), _impl->value.size());
  }
  if (!om) return BTStatus::NoMemory;

  int rc = ble_gatts_indicate_custom(connHandle, _impl->handle, om);
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
}

BLEProperty BLECharacteristic::getProperties() const {
  return _impl ? _impl->properties : BLEProperty{};
}

void BLECharacteristic::setPermissions(BLEPermission permissions) {
  BLE_CHECK_IMPL();
  impl.permissions = permissions;
}

BLEPermission BLECharacteristic::getPermissions() const {
  return _impl ? _impl->permissions : BLEPermission{};
}

BLEDescriptor BLECharacteristic::createDescriptor(const BLEUUID &uuid, BLEPermission perms, size_t maxLen) {
  BLE_CHECK_IMPL(BLEDescriptor());

  auto descImpl = std::make_shared<BLEDescriptor::Impl>();
  descImpl->uuid = uuid;
  descImpl->charImpl = _impl;
  uuidToNimble(uuid, descImpl->nimbleUUID);

  uint8_t flags = 0;
  uint16_t p = static_cast<uint16_t>(perms);
  if ((p & static_cast<uint16_t>(BLEPermission::Read)) || p == 0) flags |= BLE_ATT_F_READ;
  if (p & static_cast<uint16_t>(BLEPermission::Write)) flags |= BLE_ATT_F_WRITE;
  if (p & static_cast<uint16_t>(BLEPermission::ReadEncrypted)) flags |= BLE_ATT_F_READ | BLE_ATT_F_READ_ENC;
  if (p & static_cast<uint16_t>(BLEPermission::ReadAuthenticated)) flags |= BLE_ATT_F_READ | BLE_ATT_F_READ_AUTHEN;
  if (p & static_cast<uint16_t>(BLEPermission::ReadAuthorized)) flags |= BLE_ATT_F_READ | BLE_ATT_F_READ_AUTHOR;
  if (p & static_cast<uint16_t>(BLEPermission::WriteEncrypted)) flags |= BLE_ATT_F_WRITE | BLE_ATT_F_WRITE_ENC;
  if (p & static_cast<uint16_t>(BLEPermission::WriteAuthenticated)) flags |= BLE_ATT_F_WRITE | BLE_ATT_F_WRITE_AUTHEN;
  if (p & static_cast<uint16_t>(BLEPermission::WriteAuthorized)) flags |= BLE_ATT_F_WRITE | BLE_ATT_F_WRITE_AUTHOR;
  descImpl->attFlags = flags;
  descImpl->value.reserve(maxLen);

  impl.descriptors.push_back(descImpl);

  return BLEDescriptor(descImpl);
}

BLEDescriptor BLECharacteristic::getDescriptor(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLEDescriptor());
  for (auto &d : impl.descriptors) {
    if (d->uuid == uuid) {
      return BLEDescriptor(d);
    }
  }
  return BLEDescriptor();
}

std::vector<BLEDescriptor> BLECharacteristic::getDescriptors() const {
  std::vector<BLEDescriptor> result;
  BLE_CHECK_IMPL(result);
  for (auto &d : impl.descriptors) {
    result.push_back(BLEDescriptor(d));
  }
  return result;
}

void BLECharacteristic::removeDescriptor(const BLEDescriptor &desc) {
  if (!_impl || !desc._impl) return;
  auto &descs = _impl->descriptors;
  descs.erase(std::remove_if(descs.begin(), descs.end(),
    [&](const std::shared_ptr<BLEDescriptor::Impl> &d) { return d == desc._impl; }), descs.end());
}

size_t BLECharacteristic::getSubscribedCount() const {
  return _impl ? _impl->subscribers.size() : 0;
}

bool BLECharacteristic::isSubscribed(uint16_t connHandle) const {
  if (!_impl) return false;
  return _impl->subscriberGet(connHandle) > 0;
}

std::vector<uint16_t> BLECharacteristic::getSubscribedConnections() const {
  std::vector<uint16_t> result;
  BLE_CHECK_IMPL(result);
  for (const auto &kv : impl.subscribers) {
    if (kv.second > 0) result.push_back(kv.first);
  }
  return result;
}

BLEUUID BLECharacteristic::getUUID() const {
  return _impl ? _impl->uuid : BLEUUID();
}

uint16_t BLECharacteristic::getHandle() const {
  return _impl ? _impl->handle : 0;
}

BLEService BLECharacteristic::getService() const {
  return _impl ? BLEService(_impl->serviceImpl.lock()) : BLEService();
}

void BLECharacteristic::setDescription(const String &desc) {
  if (!_impl) return;
  auto existing = getDescriptor(BLEUUID(static_cast<uint16_t>(0x2901)));
  if (existing) {
    existing.setValue(desc);
  } else {
    auto d = createDescriptor(BLEUUID(static_cast<uint16_t>(0x2901)), BLEPermission::Read, desc.length() + 1);
    d.setValue(desc);
  }
}

String BLECharacteristic::toString() const {
  BLE_CHECK_IMPL("BLECharacteristic(null)");
  return "BLECharacteristic(uuid=" + impl.uuid.toString() + ")";
}

// --------------------------------------------------------------------------
// NimBLE GATT table builder
// --------------------------------------------------------------------------

static ble_gatt_chr_flags mapPropertyFlags(BLEProperty props, BLEPermission perms) {
  ble_gatt_chr_flags f = 0;
  uint8_t p = static_cast<uint8_t>(props);
  if (p & 0x01) f |= BLE_GATT_CHR_F_BROADCAST;
  if (p & 0x02) f |= BLE_GATT_CHR_F_READ;
  if (p & 0x04) f |= BLE_GATT_CHR_F_WRITE_NO_RSP;
  if (p & 0x08) f |= BLE_GATT_CHR_F_WRITE;
  if (p & 0x10) f |= BLE_GATT_CHR_F_NOTIFY;
  if (p & 0x20) f |= BLE_GATT_CHR_F_INDICATE;
  if (p & 0x40) f |= BLE_GATT_CHR_F_AUTH_SIGN_WRITE;

  uint16_t pm = static_cast<uint16_t>(perms);
  if (pm & 0x0002) f |= BLE_GATT_CHR_F_READ_ENC;
  if (pm & 0x0004) f |= BLE_GATT_CHR_F_READ_AUTHEN;
  if (pm & 0x0008) f |= BLE_GATT_CHR_F_READ_AUTHOR;
  if (pm & 0x0020) f |= BLE_GATT_CHR_F_WRITE_ENC;
  if (pm & 0x0040) f |= BLE_GATT_CHR_F_WRITE_AUTHEN;
  if (pm & 0x0080) f |= BLE_GATT_CHR_F_WRITE_AUTHOR;
  return f;
}

// Persistent storage for GATT table arrays (must outlive ble_gatts_start)
static std::vector<ble_gatt_svc_def> s_gattSvcs;
static std::vector<std::vector<ble_gatt_chr_def>> s_gattChrs;
static std::vector<std::vector<ble_gatt_dsc_def>> s_gattDscs;

int nimbleRegisterGattServices(
    const std::vector<std::shared_ptr<BLEService::Impl>> &services) {
  s_gattSvcs.clear();
  s_gattChrs.clear();
  s_gattDscs.clear();

  for (auto &svcImpl : services) {
    uuidToNimble(svcImpl->uuid, svcImpl->nimbleUUID);
    std::vector<ble_gatt_chr_def> chrs;

    for (auto &chrImpl : svcImpl->characteristics) {
      std::vector<ble_gatt_dsc_def> dscs;
      for (auto &dscImpl : chrImpl->descriptors) {
        ble_gatt_dsc_def d = {};
        d.uuid = &dscImpl->nimbleUUID.u;
        d.att_flags = dscImpl->attFlags;
        d.access_cb = BLECharacteristic::Impl::descAccessCallback;
        d.arg = dscImpl.get();
        dscs.push_back(d);
      }
      dscs.push_back({});
      s_gattDscs.push_back(std::move(dscs));

      ble_gatt_chr_def c = {};
      c.uuid = &chrImpl->nimbleUUID.u;
      c.access_cb = BLECharacteristic::Impl::accessCallback;
      c.arg = chrImpl.get();
      c.flags = mapPropertyFlags(chrImpl->properties, chrImpl->permissions);
      c.val_handle = &chrImpl->handle;
      c.descriptors = s_gattDscs.back().data();
      chrs.push_back(c);
    }
    chrs.push_back({});
    s_gattChrs.push_back(std::move(chrs));

    ble_gatt_svc_def s = {};
    s.type = BLE_GATT_SVC_TYPE_PRIMARY;
    s.uuid = &svcImpl->nimbleUUID.u;
    s.characteristics = s_gattChrs.back().data();
    s_gattSvcs.push_back(s);
  }
  s_gattSvcs.push_back({});

  int rc = ble_gatts_count_cfg(s_gattSvcs.data());
  if (rc != 0) {
    log_e("ble_gatts_count_cfg: rc=%d", rc);
    return rc;
  }

  rc = ble_gatts_add_svcs(s_gattSvcs.data());
  if (rc != 0) {
    log_e("ble_gatts_add_svcs: rc=%d", rc);
  }
  return rc;
}

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
