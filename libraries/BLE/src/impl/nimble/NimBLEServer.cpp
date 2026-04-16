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

#include "NimBLEServer.h"
#include "NimBLECharacteristic.h"
#include "NimBLEService.h"
#include "impl/BLEConnInfoData.h"
#include "impl/BLEImplHelpers.h"
#include "impl/BLEServerBackend.h"
#include "esp32-hal-log.h"

#include <host/ble_hs.h>
#include <host/ble_att.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

namespace {

void dispatchIdentity(BLEServer::Impl *impl, const BLEConnInfo &connInfo) {
  decltype(impl->onIdentityCb) cb;
  BLEServer::Callbacks *callbacks = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    cb = impl->onIdentityCb;
    callbacks = impl->callbacks;
  }
  BLEServer serverHandle = BLEServer::Impl::makeHandle(impl);
  if (callbacks) {
    callbacks->onIdentity(serverHandle, connInfo);
  }
  if (cb) cb(serverHandle, connInfo);
}

} // namespace

// --------------------------------------------------------------------------
// BLEConnInfoImpl -- Bridge from NimBLE ble_gap_conn_desc to BLEConnInfo
// --------------------------------------------------------------------------

BLEConnInfo BLEConnInfoImpl::fromDesc(const struct ble_gap_conn_desc &desc) {
  BLEConnInfo info;
  info._valid = true;

  auto *d = info.data();
  d->handle = desc.conn_handle;
  d->mtu = ble_att_mtu(desc.conn_handle);
  d->address = BTAddress(desc.peer_ota_addr.val, static_cast<BTAddress::Type>(desc.peer_ota_addr.type));
  d->idAddress = BTAddress(desc.peer_id_addr.val, static_cast<BTAddress::Type>(desc.peer_id_addr.type));
  d->interval = desc.conn_itvl;
  d->latency = desc.conn_latency;
  d->supervisionTimeout = desc.supervision_timeout;
  d->encrypted = desc.sec_state.encrypted;
  d->authenticated = desc.sec_state.authenticated;
  d->bonded = desc.sec_state.bonded;
  d->keySize = desc.sec_state.key_size;
  d->central = (desc.role == BLE_GAP_ROLE_MASTER);
  d->txPhy = 1;
  d->rxPhy = 1;
  d->rssi = 0;

#if BLE5_SUPPORTED
  BLEPhy tx, rx;
  int rc = ble_gap_read_le_phy(desc.conn_handle, reinterpret_cast<uint8_t *>(&tx), reinterpret_cast<uint8_t *>(&rx));
  if (rc == 0) {
    d->txPhy = static_cast<uint8_t>(tx);
    d->rxPhy = static_cast<uint8_t>(rx);
  }
#endif

  int8_t rssi;
  if (ble_gap_conn_rssi(desc.conn_handle, &rssi) == 0) {
    d->rssi = rssi;
  }

  return info;
}

// --------------------------------------------------------------------------
// BLEServer::Impl -- NimBLE backend
// --------------------------------------------------------------------------

// BLEService::Impl is defined in NimBLEService.h (shared with NimBLECharacteristic.cpp)

// --------------------------------------------------------------------------
// BLEServer start / connection management
// --------------------------------------------------------------------------

/** Full NimBLE GATT rebuild (used by @ref BLEServer::start and @ref bleServerRemoveService). */
static BTStatus nimbleRebuildGattDatabase(BLEServer::Impl &impl) {
  ble_gatts_reset();
  ble_svc_gap_init();
  ble_svc_gatt_init();

  if (!impl.services.empty()) {
    int rc = nimbleRegisterGattServices(impl.services);
    if (rc != 0) {
      log_e("nimbleRegisterGattServices: rc=%d", rc);
      return BTStatus::Fail;
    }
    for (auto &s : impl.services) {
      s->started = true;
    }
  }

  int rc = ble_gatts_start();
  if (rc != 0) {
    log_e("ble_gatts_start: rc=%d", rc);
    return BTStatus::Fail;
  }

  String name = BLE.getDeviceName();
  if (name.length() > 0) {
    ble_svc_gap_device_name_set(name.c_str());
  }

  impl.started = true;
  return BTStatus::OK;
}

BTStatus BLEServer::start() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (impl.started) {
    // Check if new services were added after the initial start
    // (characteristics with handle == 0 have not been registered).
    bool hasNew = false;
    for (auto &s : impl.services) {
      for (auto &c : s->characteristics) {
        if (c->handle == 0) {
          hasNew = true;
          break;
        }
      }
      if (hasNew) break;
    }
    if (!hasNew) return BTStatus::OK;
    log_d("Server: re-registering GATT services (new characteristics detected)");
  } else {
    log_d("Server: starting with %u service(s)", (unsigned)impl.services.size());
  }

  BTStatus st = nimbleRebuildGattDatabase(impl);
  if (st == BTStatus::OK) {
    log_i("Server: started, %u service(s) registered", (unsigned)impl.services.size());
  }
  return st;
}

BTStatus BLEServer::disconnect(uint16_t connHandle, uint8_t reason) {
  if (!_impl) {
    log_w("Server: disconnect called with no impl");
    return BTStatus::InvalidState;
  }
  int rc = ble_gap_terminate(connHandle, reason);
  if (rc != 0) {
    log_e("Server: ble_gap_terminate handle=%u rc=%d", connHandle, rc);
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

BTStatus BLEServer::connect(const BTAddress &address) {
  log_w("Server: connect not supported on NimBLE (server role only)");
  return BTStatus::NotSupported;
}

uint16_t BLEServer::getPeerMTU(uint16_t connHandle) const {
  return ble_att_mtu(connHandle);
}

BTStatus BLEServer::updateConnParams(uint16_t connHandle, const BLEConnParams &params) {
  if (!_impl) {
    log_w("Server: updateConnParams called with no impl");
    return BTStatus::InvalidState;
  }
  struct ble_gap_upd_params nimParams = {};
  nimParams.itvl_min = params.minInterval;
  nimParams.itvl_max = params.maxInterval;
  nimParams.latency = params.latency;
  nimParams.supervision_timeout = params.timeout;
  int rc = ble_gap_update_params(connHandle, &nimParams);
  if (rc != 0) {
    log_e("Server: ble_gap_update_params handle=%u rc=%d", connHandle, rc);
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

BTStatus BLEServer::setPhy(uint16_t connHandle, BLEPhy txPhy, BLEPhy rxPhy) {
#if BLE5_SUPPORTED
  int rc = ble_gap_set_prefered_le_phy(connHandle, static_cast<uint8_t>(txPhy), static_cast<uint8_t>(rxPhy), 0);
  if (rc != 0) {
    log_e("Server: ble_gap_set_prefered_le_phy handle=%u rc=%d", connHandle, rc);
    return BTStatus::Fail;
  }
  return BTStatus::OK;
#else
  log_w("Server: setPhy not supported (BLE 5.0 unavailable)");
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEServer::getPhy(uint16_t connHandle, BLEPhy &txPhy, BLEPhy &rxPhy) const {
#if BLE5_SUPPORTED
  uint8_t tx, rx;
  int rc = ble_gap_read_le_phy(connHandle, &tx, &rx);
  if (rc == 0) {
    txPhy = static_cast<BLEPhy>(tx);
    rxPhy = static_cast<BLEPhy>(rx);
    return BTStatus::OK;
  }
  log_e("Server: ble_gap_read_le_phy handle=%u rc=%d", connHandle, rc);
  return BTStatus::Fail;
#else
  log_w("Server: getPhy not supported (BLE 5.0 unavailable)");
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEServer::setDataLen(uint16_t connHandle, uint16_t txOctets, uint16_t txTime) {
  int rc = ble_gap_set_data_len(connHandle, txOctets, txTime);
  if (rc != 0) {
    log_e("Server: ble_gap_set_data_len handle=%u rc=%d", connHandle, rc);
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

// --------------------------------------------------------------------------
// GAP event handler
// --------------------------------------------------------------------------

int BLEServer::Impl::gapEventHandler(struct ble_gap_event *event, void *arg) {
  auto *impl = static_cast<BLEServer::Impl *>(arg);
  if (!impl) {
    return 0;
  }

  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
      if (event->connect.status != 0) {
        log_e("Server: connection failed, status=%d", event->connect.status);
        return 0;
      }

      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
      if (rc != 0) {
        return 0;
      }

      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(desc);

      {
        BLELockGuard lock(impl->mtx);
        impl->connSet(event->connect.conn_handle, connInfo);
      }
      ble_server_dispatch::dispatchConnect(impl, connInfo);
      return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT: {
      uint16_t connHandle = event->disconnect.conn.conn_handle;
      uint8_t reason = event->disconnect.reason;
      log_d("Server: disconnect event, handle=%u reason=0x%02x", connHandle, reason);

      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(event->disconnect.conn);

      bool shouldAdvertise = false;
      {
        BLELockGuard lock(impl->mtx);
        impl->connErase(connHandle);
        for (auto &svc : impl->services) {
          for (auto &chr : svc->characteristics) {
            auto &subs = chr->subscribers;
            for (auto it = subs.begin(); it != subs.end(); ++it) {
              if (it->first == connHandle) {
                subs.erase(it);
                break;
              }
            }
          }
        }
        shouldAdvertise = impl->advertiseOnDisconnect;
      }
      ble_server_dispatch::dispatchDisconnect(impl, connInfo, reason);

      if (shouldAdvertise) {
        BLE.startAdvertising();
      }
      return 0;
    }

    case BLE_GAP_EVENT_MTU: {
      uint16_t connHandle = event->mtu.conn_handle;
      uint16_t mtu = event->mtu.value;

      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(connHandle, &desc);
      if (rc != 0) {
        log_w("MTU event: conn_find failed for handle %u", connHandle);
        return 0;
      }
      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(desc);

      {
        BLELockGuard lock(impl->mtx);
        if (impl->connFind(connHandle)) {
          impl->connSet(connHandle, connInfo);
        }
      }
      ble_server_dispatch::dispatchMtuChanged(impl, connInfo, mtu);
      return 0;
    }

    case BLE_GAP_EVENT_CONN_UPDATE: {
      if (event->conn_update.status != 0) {
        return 0;
      }
      uint16_t connHandle = event->conn_update.conn_handle;
      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(connHandle, &desc);
      if (rc != 0) {
        return 0;
      }

      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(desc);

      {
        BLELockGuard lock(impl->mtx);
        if (impl->connFind(connHandle)) {
          impl->connSet(connHandle, connInfo);
        }
      }
      ble_server_dispatch::dispatchConnParamsUpdate(impl, connInfo);
      return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE: {
      uint16_t connHandle = event->enc_change.conn_handle;
      int status = event->enc_change.status;

      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(connHandle, &desc);
      if (rc != 0) {
        return 0;
      }

      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(desc);

      {
        BLELockGuard lock(impl->mtx);
        if (impl->connFind(connHandle)) {
          impl->connSet(connHandle, connInfo);
        }
      }
      dispatchIdentity(impl, connInfo);

      BLESecurity sec = BLE.getSecurity();
      if (sec) {
        sec.notifyAuthComplete(connInfo, status == 0);
      }
      return 0;
    }

    case BLE_GAP_EVENT_IDENTITY_RESOLVED: {
      uint16_t connHandle = event->identity_resolved.conn_handle;

      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(connHandle, &desc);
      if (rc != 0) {
        return 0;
      }

      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(desc);

      {
        BLELockGuard lock(impl->mtx);
        if (impl->connFind(connHandle)) {
          impl->connSet(connHandle, connInfo);
        }
      }
      dispatchIdentity(impl, connInfo);
      return 0;
    }

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
      uint16_t connHandle = event->passkey.conn_handle;

      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(connHandle, &desc);
      if (rc != 0) {
        return 0;
      }

      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(desc);
      BLESecurity sec = BLE.getSecurity();
      if (!sec) {
        return 0;
      }

      if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
        struct ble_sm_io pkey = {};
        pkey.action = BLE_SM_IOACT_DISP;
        pkey.passkey = sec.resolvePasskeyForDisplay(connInfo);
        ble_sm_inject_io(connHandle, &pkey);
      } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
        struct ble_sm_io pkey = {};
        pkey.action = BLE_SM_IOACT_INPUT;
        pkey.passkey = sec.resolvePasskeyForInput(connInfo);
        ble_sm_inject_io(connHandle, &pkey);
      } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
        struct ble_sm_io pkey = {};
        pkey.action = BLE_SM_IOACT_NUMCMP;
        pkey.numcmp_accept = sec.resolveNumericComparison(connInfo, event->passkey.params.numcmp) ? 1 : 0;
        ble_sm_inject_io(connHandle, &pkey);
      }
      return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
      if (rc != 0) {
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
      }

      ble_store_util_delete_peer(&desc.peer_id_addr);
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_SUBSCRIBE: {
      uint16_t connHandle = event->subscribe.conn_handle;
      uint16_t attrHandle = event->subscribe.attr_handle;
      uint8_t curNotify = event->subscribe.cur_notify;
      uint8_t curIndicate = event->subscribe.cur_indicate;
      uint16_t subVal = (curIndicate ? 0x0002 : 0) | (curNotify ? 0x0001 : 0);
      log_d("Server: subscribe event, conn=%u attr=%u notify=%d indicate=%d subVal=0x%04x",
            connHandle, attrHandle, curNotify, curIndicate, subVal);

      BLELockGuard lock(impl->mtx);
      for (auto &svc : impl->services) {
        for (auto &chr : svc->characteristics) {
          if (chr->handle == attrHandle) {
            if (subVal > 0) {
              bool found = false;
              for (auto &kv : chr->subscribers) {
                if (kv.first == connHandle) {
                  kv.second = subVal;
                  found = true;
                  break;
                }
              }
              if (!found) {
                chr->subscribers.emplace_back(connHandle, subVal);
              }
            } else {
              auto &subs = chr->subscribers;
              for (auto it = subs.begin(); it != subs.end(); ++it) {
                if (it->first == connHandle) {
                  subs.erase(it);
                  break;
                }
              }
            }
            if (chr->onSubscribeCb) {
              BLECharacteristic characteristic(chr);
              struct ble_gap_conn_desc desc;
              if (ble_gap_conn_find(connHandle, &desc) == 0) {
                BLEConnInfo info = BLEConnInfoImpl::fromDesc(desc);
                chr->onSubscribeCb(characteristic, info, subVal);
              }
            }
            return 0;
          }
        }
      }
      return 0;
    }

    case BLE_GAP_EVENT_NOTIFY_TX: {
      return 0;
    }

    default: return 0;
  }
}

int BLEServer::handleGapEvent(void *rawEvent) {
  if (!_impl || !rawEvent) return 0;
  return Impl::gapEventHandler(static_cast<struct ble_gap_event *>(rawEvent), _impl.get());
}

// BLEService public API methods are in NimBLECharacteristic.cpp

// --------------------------------------------------------------------------
// BLEClass::createServer() -- NimBLE factory method
// --------------------------------------------------------------------------

BLEServer BLEClass::createServer() {
  if (!isInitialized()) {
    log_e("createServer: BLE not initialized");
    return BLEServer();
  }

  // Singleton server: use a static shared_ptr
  static std::shared_ptr<BLEServer::Impl> server;
  if (!server) {
    log_d("createServer: creating new server instance");
    server = std::make_shared<BLEServer::Impl>();
  }

  return BLEServer(server);
}

// --------------------------------------------------------------------------
// Dynamic service removal (NimBLE has no single-service delete; rebuild GATT)
// --------------------------------------------------------------------------

BTStatus bleServerRemoveService(BLEServer::Impl *impl, std::shared_ptr<BLEService::Impl> svc) {
  if (!impl || !svc) return BTStatus::InvalidState;

  bool inList = false;
  {
    BLELockGuard lock(impl->mtx);
    for (auto &s : impl->services) {
      if (s.get() == svc.get()) {
        inList = true;
        break;
      }
    }
  }
  if (!inList) return BTStatus::InvalidState;

  {
    BLELockGuard lock(impl->mtx);
    for (auto it = impl->services.begin(); it != impl->services.end(); ++it) {
      if (it->get() == svc.get()) {
        impl->services.erase(it);
        break;
      }
    }
  }

  if (!impl->started) {
    return BTStatus::OK;
  }

  BTStatus st = nimbleRebuildGattDatabase(*impl);
  if (st == BTStatus::OK) {
    log_i("Server: service removed, %u service(s) registered", (unsigned)impl->services.size());
  }
  return st;
}

#endif /* BLE_NIMBLE */
