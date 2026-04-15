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

#include "NimBLECharacteristic.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

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
      log_d("Characteristic %s: read by conn=%u", impl->uuid.toString().c_str(), conn_handle);
      if (conn_handle != BLE_HS_CONN_HANDLE_NONE && impl->onReadCb) {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(conn_handle, &desc) == 0) {
          BLECharacteristic chr{std::shared_ptr<BLECharacteristic::Impl>(impl, [](BLECharacteristic::Impl *){})};
          impl->onReadCb(chr, BLEConnInfoImpl::fromDesc(desc));
        }
      }

      BLELockGuard lock(impl->valueMtx);
      int rc = os_mbuf_append(ctxt->om, impl->value.data(), impl->value.size());
      return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
      uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
      if (len > BLE_ATT_ATTR_MAX_LEN) {
        log_w("Characteristic %s: write len=%u exceeds max", impl->uuid.toString().c_str(), len);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      log_d("Characteristic %s: write by conn=%u len=%u", impl->uuid.toString().c_str(), conn_handle, len);
      uint8_t buf[BLE_ATT_ATTR_MAX_LEN];
      os_mbuf_copydata(ctxt->om, 0, len, buf);

      {
        BLELockGuard lock(impl->valueMtx);
        impl->value.assign(buf, buf + len);
      }

      if (impl->onWriteCb) {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(conn_handle, &desc) == 0) {
          BLECharacteristic chr{std::shared_ptr<BLECharacteristic::Impl>(impl, [](BLECharacteristic::Impl *){})};
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
  auto *desc = static_cast<BLEDescriptor::Impl *>(arg);
  if (!desc) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_DSC: {
      if (desc->onReadCb && conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct ble_gap_conn_desc conn_desc;
        if (ble_gap_conn_find(conn_handle, &conn_desc) == 0) {
          BLEDescriptor dsc{std::shared_ptr<BLEDescriptor::Impl>(desc, [](BLEDescriptor::Impl *){})};
          desc->onReadCb(dsc, BLEConnInfoImpl::fromDesc(conn_desc));
        }
      }
      BLELockGuard lock(desc->mtx);
      int rc = os_mbuf_append(ctxt->om, desc->value.data(), desc->value.size());
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
        BLELockGuard lock(desc->mtx);
        desc->value.assign(buf, buf + len);
      }
      if (desc->onWriteCb && conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct ble_gap_conn_desc conn_desc;
        if (ble_gap_conn_find(conn_handle, &conn_desc) == 0) {
          BLEDescriptor dsc{std::shared_ptr<BLEDescriptor::Impl>(desc, [](BLEDescriptor::Impl *){})};
          desc->onWriteCb(dsc, BLEConnInfoImpl::fromDesc(conn_desc));
        }
      }
      return 0;
    }

    default: return BLE_ATT_ERR_UNLIKELY;
  }
}

// --------------------------------------------------------------------------
// BLECharacteristic public API
// --------------------------------------------------------------------------

BTStatus BLECharacteristic::notify(const uint8_t *data, size_t length) {
  if (!_impl || _impl->handle == 0) {
    log_w("Characteristic: notify called but not registered (handle=0)");
    return BTStatus::InvalidState;
  }

  for (auto &[connHandle, subVal] : _impl->subscribers) {
    if (subVal & 0x0001) {
      notify(connHandle, data, length);
    }
  }
  return BTStatus::OK;
}

BTStatus BLECharacteristic::notify(uint16_t connHandle, const uint8_t *data, size_t length) {
  if (!_impl || _impl->handle == 0) {
    log_w("Characteristic: notify(conn) called but not registered (handle=0)");
    return BTStatus::InvalidState;
  }

  log_d("Characteristic %s: notify conn=%u len=%u", _impl->uuid.toString().c_str(), connHandle, length);
  struct os_mbuf *om = nullptr;
  if (data && length > 0) {
    om = ble_hs_mbuf_from_flat(data, length);
  } else {
    BLELockGuard lock(_impl->valueMtx);
    om = ble_hs_mbuf_from_flat(_impl->value.data(), _impl->value.size());
  }
  if (!om) {
    log_e("Characteristic %s: notify - no memory for mbuf", _impl->uuid.toString().c_str());
    return BTStatus::NoMemory;
  }

  int rc = ble_gatts_notify_custom(connHandle, _impl->handle, om);
  if (rc != 0) {
    log_w("Characteristic %s: notify failed for conn=%u rc=%d", _impl->uuid.toString().c_str(), connHandle, rc);
  }
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
}

BTStatus BLECharacteristic::indicate(const uint8_t *data, size_t length) {
  if (!_impl || _impl->handle == 0) {
    log_w("Characteristic: indicate called but not registered (handle=0)");
    return BTStatus::InvalidState;
  }

  for (auto &[connHandle, subVal] : _impl->subscribers) {
    if (subVal & 0x0002) {
      indicate(connHandle, data, length);
    }
  }
  return BTStatus::OK;
}

BTStatus BLECharacteristic::indicate(uint16_t connHandle, const uint8_t *data, size_t length) {
  if (!_impl || _impl->handle == 0) {
    log_w("Characteristic: indicate(conn) called but not registered (handle=0)");
    return BTStatus::InvalidState;
  }

  log_d("Characteristic %s: indicate conn=%u len=%u", _impl->uuid.toString().c_str(), connHandle, length);
  struct os_mbuf *om = nullptr;
  if (data && length > 0) {
    om = ble_hs_mbuf_from_flat(data, length);
  } else {
    BLELockGuard lock(_impl->valueMtx);
    om = ble_hs_mbuf_from_flat(_impl->value.data(), _impl->value.size());
  }
  if (!om) {
    log_e("Characteristic %s: indicate - no memory for mbuf", _impl->uuid.toString().c_str());
    return BTStatus::NoMemory;
  }

  int rc = ble_gatts_indicate_custom(connHandle, _impl->handle, om);
  if (rc != 0) {
    log_w("Characteristic %s: indicate failed for conn=%u rc=%d", _impl->uuid.toString().c_str(), connHandle, rc);
  }
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
}

BLEDescriptor BLECharacteristic::createDescriptor(const BLEUUID &uuid, BLEPermission perms, size_t maxLen) {
  BLE_CHECK_IMPL(BLEDescriptor());

  auto desc = std::make_shared<BLEDescriptor::Impl>();
  desc->uuid = uuid;
  desc->chr = _impl.get();
  uuidToNimble(uuid, desc->nimbleUUID);

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
  desc->attFlags = flags;
  desc->value.reserve(maxLen);

  impl.descriptors.push_back(desc);

  return BLEDescriptor(desc);
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
  log_d("GATT: registering %u service(s)", (unsigned)services.size());
  s_gattSvcs.clear();
  s_gattChrs.clear();
  s_gattDscs.clear();

  for (auto &svc : services) {
    log_d("GATT: service %s with %u characteristic(s)", svc->uuid.toString().c_str(), (unsigned)svc->characteristics.size());
    uuidToNimble(svc->uuid, svc->nimbleUUID);
    std::vector<ble_gatt_chr_def> chrs;

    for (auto &chr : svc->characteristics) {
      uuidToNimble(chr->uuid, chr->nimbleUUID);
      std::vector<ble_gatt_dsc_def> dscs;
      for (auto &dscImpl : chr->descriptors) {
        uuidToNimble(dscImpl->uuid, dscImpl->nimbleUUID);
        // Set default attFlags if not explicitly configured
        if (dscImpl->attFlags == 0) {
          if (dscImpl->uuid == BLEUUID(static_cast<uint16_t>(0x2902))) {
            dscImpl->attFlags = BLE_ATT_F_READ | BLE_ATT_F_WRITE;
          } else {
            dscImpl->attFlags = BLE_ATT_F_READ;
          }
        }
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
      c.uuid = &chr->nimbleUUID.u;
      c.access_cb = BLECharacteristic::Impl::accessCallback;
      c.arg = chr.get();
      c.flags = mapPropertyFlags(chr->properties, chr->permissions);
      c.val_handle = &chr->handle;
      c.descriptors = s_gattDscs.back().data();
      chrs.push_back(c);
    }
    chrs.push_back({});
    s_gattChrs.push_back(std::move(chrs));

    ble_gatt_svc_def s = {};
    s.type = BLE_GATT_SVC_TYPE_PRIMARY;
    s.uuid = &svc->nimbleUUID.u;
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

#endif /* BLE_NIMBLE */
