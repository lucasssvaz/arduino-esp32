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

#include "NimBLEClient.h"
#include "NimBLERemoteTypes.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <host/ble_att.h>

namespace {

void dispatchConnectFail(BLEClient::Impl *impl, int reason) {
  decltype(impl->onConnectFailCb) cb;
  BLEClient::Callbacks *callbacks = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    cb = impl->onConnectFailCb;
    callbacks = impl->callbacks;
  }
  BLEClient clientHandle = BLEClient::Impl::makeHandle(impl);
  if (callbacks) {
    callbacks->onConnectFail(clientHandle, reason);
  }
  if (cb) cb(clientHandle, reason);
}

void dispatchConnect(BLEClient::Impl *impl, const BLEConnInfo &connInfo) {
  decltype(impl->onConnectCb) cb;
  BLEClient::Callbacks *callbacks = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    cb = impl->onConnectCb;
    callbacks = impl->callbacks;
  }
  BLEClient clientHandle = BLEClient::Impl::makeHandle(impl);
  if (callbacks) {
    callbacks->onConnect(clientHandle, connInfo);
  }
  if (cb) cb(clientHandle, connInfo);
}

void dispatchDisconnect(BLEClient::Impl *impl, const BLEConnInfo &connInfo, uint8_t reason) {
  decltype(impl->onDisconnectCb) cb;
  BLEClient::Callbacks *callbacks = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    cb = impl->onDisconnectCb;
    callbacks = impl->callbacks;
  }
  BLEClient clientHandle = BLEClient::Impl::makeHandle(impl);
  if (callbacks) {
    callbacks->onDisconnect(clientHandle, connInfo, reason);
  }
  if (cb) cb(clientHandle, connInfo, reason);
}

void dispatchMtuChanged(BLEClient::Impl *impl, const BLEConnInfo &connInfo, uint16_t mtu) {
  decltype(impl->onMtuChangedCb) cb;
  BLEClient::Callbacks *callbacks = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    cb = impl->onMtuChangedCb;
    callbacks = impl->callbacks;
  }
  BLEClient clientHandle = BLEClient::Impl::makeHandle(impl);
  if (callbacks) {
    callbacks->onMtuChanged(clientHandle, connInfo, mtu);
  }
  if (cb) cb(clientHandle, connInfo, mtu);
}

bool dispatchConnParamsRequest(BLEClient::Impl *impl, const BLEConnParams &params) {
  decltype(impl->onConnParamsReqCb) cb;
  BLEClient::Callbacks *callbacks = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    cb = impl->onConnParamsReqCb;
    callbacks = impl->callbacks;
  }
  BLEClient clientHandle = BLEClient::Impl::makeHandle(impl);
  bool accept = true;
  if (callbacks) {
    accept = callbacks->onConnParamsUpdateRequest(clientHandle, params);
  }
  if (accept && cb) {
    accept = cb(clientHandle, params);
  }
  return accept;
}

void dispatchIdentity(BLEClient::Impl *impl, const BLEConnInfo &connInfo) {
  decltype(impl->onIdentityCb) cb;
  BLEClient::Callbacks *callbacks = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    cb = impl->onIdentityCb;
    callbacks = impl->callbacks;
  }
  BLEClient clientHandle = BLEClient::Impl::makeHandle(impl);
  if (callbacks) {
    callbacks->onIdentity(clientHandle, connInfo);
  }
  if (cb) cb(clientHandle, connInfo);
}

} // namespace

// --------------------------------------------------------------------------
// BLEClient public API
// --------------------------------------------------------------------------

BTStatus BLEClient::connect(const BTAddress &address, uint32_t timeoutMs) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (impl.connected) return BTStatus::AlreadyConnected;

  impl.peerAddress = address;
  impl.connectSync.take();

  ble_addr_t addr;
  addr.type = static_cast<uint8_t>(address.type());
  memcpy(addr.val, address.data(), 6);

  int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, timeoutMs, NULL, Impl::gapEventHandler, _impl.get());
  if (rc != 0) {
    impl.connectSync.give(BTStatus::Fail);
    log_e("ble_gap_connect: rc=%d", rc);
    return BTStatus::Fail;
  }

  BTStatus status = impl.connectSync.wait(timeoutMs + 500);
  if (status != BTStatus::OK) {
    ble_gap_conn_cancel();
    return status;
  }

  if (impl.preferredMTU > 0 || ble_att_preferred_mtu() > BLE_ATT_MTU_DFLT) {
    ble_gattc_exchange_mtu(impl.connHandle, Impl::mtuExchangeCb, _impl.get());
  }

  return BTStatus::OK;
}

BTStatus BLEClient::connect(const BLEAdvertisedDevice &device, uint32_t timeoutMs) {
  if (!device) return BTStatus::InvalidParam;
  return connect(device.getAddress(), timeoutMs);
}

BTStatus BLEClient::connect(const BTAddress &address, BLEPhy phy, uint32_t timeoutMs) {
#if BLE5_SUPPORTED
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (impl.connected) return BTStatus::AlreadyConnected;

  impl.peerAddress = address;
  impl.connectSync.take();

  ble_addr_t addr;
  addr.type = static_cast<uint8_t>(address.type());
  memcpy(addr.val, address.data(), 6);

  struct ble_gap_ext_conn_params extParams[3] = {};
  uint8_t phyMask = static_cast<uint8_t>(phy);

  for (int i = 0; i < 3; i++) {
    extParams[i].scan_itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
    extParams[i].scan_window = BLE_GAP_SCAN_FAST_WINDOW;
    extParams[i].itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN;
    extParams[i].itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX;
    extParams[i].latency = 0;
    extParams[i].supervision_timeout = 0x0100;
    extParams[i].min_ce_len = 0;
    extParams[i].max_ce_len = 0;
  }

  int rc = ble_gap_ext_connect(BLE_OWN_ADDR_PUBLIC, &addr, timeoutMs, phyMask, extParams, extParams + 1, extParams + 2,
                               Impl::gapEventHandler, _impl.get());
  if (rc != 0) {
    log_e("ble_gap_ext_connect: rc=%d", rc);
    return BTStatus::Fail;
  }

  BTStatus status = impl.connectSync.wait(timeoutMs + 500);
  if (status != BTStatus::OK) {
    ble_gap_conn_cancel();
    return status;
  }

  if (impl.preferredMTU > 0 || ble_att_preferred_mtu() > BLE_ATT_MTU_DFLT) {
    ble_gattc_exchange_mtu(impl.connHandle, Impl::mtuExchangeCb, _impl.get());
  }

  return BTStatus::OK;
#else
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClient::connect(const BLEAdvertisedDevice & /*device*/, BLEPhy /*phy*/, uint32_t /*timeoutMs*/) {
  return BTStatus::NotSupported;
}

BTStatus BLEClient::connectAsync(const BTAddress &address, BLEPhy phy) {
#if BLE5_SUPPORTED
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (impl.connected) return BTStatus::AlreadyConnected;

  impl.peerAddress = address;

  ble_addr_t addr;
  addr.type = static_cast<uint8_t>(address.type());
  memcpy(addr.val, address.data(), 6);

  struct ble_gap_ext_conn_params extParams[3] = {};
  uint8_t phyMask = static_cast<uint8_t>(phy);

  for (int i = 0; i < 3; i++) {
    extParams[i].scan_itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
    extParams[i].scan_window = BLE_GAP_SCAN_FAST_WINDOW;
    extParams[i].itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN;
    extParams[i].itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX;
    extParams[i].latency = 0;
    extParams[i].supervision_timeout = 0x0100;
    extParams[i].min_ce_len = 0;
    extParams[i].max_ce_len = 0;
  }

  int rc = ble_gap_ext_connect(BLE_OWN_ADDR_PUBLIC, &addr, 30000, phyMask, extParams, extParams + 1, extParams + 2,
                               Impl::gapEventHandler, _impl.get());
  if (rc != 0) {
    log_e("connectAsync: rc=%d", rc);
    return BTStatus::Fail;
  }
  return BTStatus::OK;
#else
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClient::connectAsync(const BLEAdvertisedDevice & /*device*/, BLEPhy /*phy*/) {
  return BTStatus::NotSupported;
}

BTStatus BLEClient::cancelConnect() {
  int rc = ble_gap_conn_cancel();
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
}

BTStatus BLEClient::disconnect() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (!impl.connected) return BTStatus::InvalidState;
  int rc = ble_gap_terminate(impl.connHandle, BLE_ERR_REM_USER_CONN_TERM);
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
}

BTStatus BLEClient::secureConnection() {
  if (!_impl || !_impl->connected) return BTStatus::InvalidState;
  int rc = ble_gap_security_initiate(_impl->connHandle);
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
}

// --------------------------------------------------------------------------
// Service discovery
// --------------------------------------------------------------------------

BTStatus BLEClient::discoverServices() {
  if (!_impl || !_impl->connected) return BTStatus::InvalidState;

  _impl->discoveredServices.clear();
  _impl->discoverSync.take();

  int rc = ble_gattc_disc_all_svcs(_impl->connHandle, Impl::serviceDiscoveryCb, _impl.get());
  if (rc != 0) {
    _impl->discoverSync.give(BTStatus::Fail);
    log_e("ble_gattc_disc_all_svcs: rc=%d", rc);
    return BTStatus::Fail;
  }

  BTStatus status = _impl->discoverSync.wait(10000);
  return status;
}

BLERemoteService BLEClient::getService(const BLEUUID &uuid) {
  if (!_impl || !_impl->connected) return BLERemoteService();

  if (_impl->discoveredServices.empty()) {
    if (discoverServices() != BTStatus::OK) return BLERemoteService();
  }

  BLEUUID target = uuid.to128();
  for (auto &entry : _impl->discoveredServices) {
    if (entry.uuid.to128() == target) {
      auto sImpl = std::make_shared<BLERemoteService::Impl>();
      sImpl->uuid = entry.uuid;
      sImpl->startHandle = entry.startHandle;
      sImpl->endHandle = entry.endHandle;
      sImpl->connHandle = _impl->connHandle;
      sImpl->client = _impl.get();

      return BLERemoteService(sImpl);
    }
  }
  return BLERemoteService();
}

std::vector<BLERemoteService> BLEClient::getServices() const {
  std::vector<BLERemoteService> result;
  BLE_CHECK_IMPL(result);

  for (auto &entry : impl.discoveredServices) {
    auto sImpl = std::make_shared<BLERemoteService::Impl>();
    sImpl->uuid = entry.uuid;
    sImpl->startHandle = entry.startHandle;
    sImpl->endHandle = entry.endHandle;
    sImpl->connHandle = impl.connHandle;
    sImpl->client = _impl.get();

    result.push_back(BLERemoteService(sImpl));
  }
  return result;
}

// --------------------------------------------------------------------------
// Connection info
// --------------------------------------------------------------------------

void BLEClient::setMTU(uint16_t mtu) {
  BLE_CHECK_IMPL();
  impl.preferredMTU = mtu;
  if (impl.connected) {
    ble_gattc_exchange_mtu(impl.connHandle, Impl::mtuExchangeCb, _impl.get());
  }
}

uint16_t BLEClient::getMTU() const {
  if (!_impl || !_impl->connected) return BLE_ATT_MTU_DFLT;
  return ble_att_mtu(_impl->connHandle);
}

int8_t BLEClient::getRSSI() const {
  if (!_impl || !_impl->connected) return -128;
  int8_t rssi;
  int rc = ble_gap_conn_rssi(_impl->connHandle, &rssi);
  return (rc == 0) ? rssi : -128;
}

uint16_t BLEClient::getHandle() const {
  return _impl ? _impl->connHandle : BLE_HS_CONN_HANDLE_NONE;
}

BLEConnInfo BLEClient::getConnection() const {
  if (!_impl || !_impl->connected) return BLEConnInfo();
  struct ble_gap_conn_desc desc;
  int rc = ble_gap_conn_find(_impl->connHandle, &desc);
  if (rc != 0) return BLEConnInfo();
  return BLEConnInfoImpl::fromDesc(desc);
}

BTStatus BLEClient::updateConnParams(const BLEConnParams &params) {
  if (!_impl || !_impl->connected) return BTStatus::InvalidState;
  struct ble_gap_upd_params nimParams = {};
  nimParams.itvl_min = params.minInterval;
  nimParams.itvl_max = params.maxInterval;
  nimParams.latency = params.latency;
  nimParams.supervision_timeout = params.timeout;
  int rc = ble_gap_update_params(_impl->connHandle, &nimParams);
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
}

BTStatus BLEClient::setPhy(BLEPhy txPhy, BLEPhy rxPhy) {
#if BLE5_SUPPORTED
  if (!_impl || !_impl->connected) return BTStatus::InvalidState;
  int rc = ble_gap_set_prefered_le_phy(_impl->connHandle, static_cast<uint8_t>(txPhy), static_cast<uint8_t>(rxPhy), 0);
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
#else
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClient::getPhy(BLEPhy &txPhy, BLEPhy &rxPhy) const {
#if BLE5_SUPPORTED
  if (!_impl || !_impl->connected) return BTStatus::InvalidState;
  uint8_t tx, rx;
  int rc = ble_gap_read_le_phy(_impl->connHandle, &tx, &rx);
  if (rc == 0) {
    txPhy = static_cast<BLEPhy>(tx);
    rxPhy = static_cast<BLEPhy>(rx);
    return BTStatus::OK;
  }
  return BTStatus::Fail;
#else
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClient::setDataLen(uint16_t txOctets, uint16_t txTime) {
  if (!_impl || !_impl->connected) return BTStatus::InvalidState;
  int rc = ble_gap_set_data_len(_impl->connHandle, txOctets, txTime);
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
}

// --------------------------------------------------------------------------
// GAP event handler
// --------------------------------------------------------------------------

int BLEClient::Impl::gapEventHandler(struct ble_gap_event *event, void *arg) {
  auto *impl = static_cast<BLEClient::Impl *>(arg);
  if (!impl) return 0;

  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
      if (event->connect.status != 0) {
        log_e("Client connection failed, status=%d", event->connect.status);
        impl->connected = false;
        impl->connHandle = BLE_HS_CONN_HANDLE_NONE;
        impl->connectSync.give(BTStatus::Fail);
        dispatchConnectFail(impl, event->connect.status);
        return 0;
      }

      impl->connHandle = event->connect.conn_handle;
      impl->connected = true;
      impl->connectSync.give(BTStatus::OK);

      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
      if (rc != 0) return 0;

      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(desc);
      dispatchConnect(impl, connInfo);
      return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT: {
      if (event->disconnect.conn.conn_handle != impl->connHandle) return 0;

      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(event->disconnect.conn);
      uint8_t reason = event->disconnect.reason;

      impl->connected = false;
      impl->connHandle = BLE_HS_CONN_HANDLE_NONE;

      dispatchDisconnect(impl, connInfo, reason);
      return 0;
    }

    case BLE_GAP_EVENT_MTU: {
      if (event->mtu.conn_handle != impl->connHandle) return 0;

      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(event->mtu.conn_handle, &desc);
      if (rc != 0) return 0;

      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(desc);

      dispatchMtuChanged(impl, connInfo, event->mtu.value);
      return 0;
    }

    case BLE_GAP_EVENT_CONN_UPDATE_REQ: {
      if (event->conn_update_req.conn_handle != impl->connHandle) return 0;

      const auto *peer = event->conn_update_req.peer_params;
      BLEConnParams params;
      params.minInterval = peer->itvl_min;
      params.maxInterval = peer->itvl_max;
      params.latency = peer->latency;
      params.timeout = peer->supervision_timeout;

      if (!dispatchConnParamsRequest(impl, params)) {
        return BLE_ERR_CONN_PARMS;
      }
      return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
    case BLE_GAP_EVENT_IDENTITY_RESOLVED: {
      uint16_t connHandle = (event->type == BLE_GAP_EVENT_ENC_CHANGE)
        ? event->enc_change.conn_handle
        : event->identity_resolved.conn_handle;
      if (connHandle != impl->connHandle) return 0;

      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(connHandle, &desc);
      if (rc != 0) return 0;

      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(desc);

      dispatchIdentity(impl, connInfo);

      if (event->type == BLE_GAP_EVENT_ENC_CHANGE) {
        BLESecurity sec = BLE.getSecurity();
        if (sec) {
          sec.notifyAuthComplete(connInfo, event->enc_change.status == 0);
        }
      }
      return 0;
    }

    case BLE_GAP_EVENT_NOTIFY_RX: {
      BLERemoteCharacteristic::Impl::handleNotifyRx(
        event->notify_rx.conn_handle,
        event->notify_rx.attr_handle,
        event->notify_rx.om,
        event->notify_rx.indication == 0
      );
      return 0;
    }

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
      struct ble_gap_conn_desc desc;
      int rc = ble_gap_conn_find(event->passkey.conn_handle, &desc);
      if (rc != 0) return 0;

      BLEConnInfo connInfo = BLEConnInfoImpl::fromDesc(desc);
      BLESecurity sec = BLE.getSecurity();
      if (!sec) return 0;

      if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
        struct ble_sm_io pkey = {};
        pkey.action = BLE_SM_IOACT_DISP;
        pkey.passkey = sec.resolvePasskeyForDisplay(connInfo);
        ble_sm_inject_io(event->passkey.conn_handle, &pkey);
      } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
        struct ble_sm_io pkey = {};
        pkey.action = BLE_SM_IOACT_INPUT;
        pkey.passkey = sec.resolvePasskeyForInput(connInfo);
        ble_sm_inject_io(event->passkey.conn_handle, &pkey);
      } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
        struct ble_sm_io pkey = {};
        pkey.action = BLE_SM_IOACT_NUMCMP;
        pkey.numcmp_accept = sec.resolveNumericComparison(connInfo, event->passkey.params.numcmp) ? 1 : 0;
        ble_sm_inject_io(event->passkey.conn_handle, &pkey);
      }
      return 0;
    }

    default: return 0;
  }
}

// --------------------------------------------------------------------------
// Service discovery callback
// --------------------------------------------------------------------------

int BLEClient::Impl::serviceDiscoveryCb(uint16_t connHandle, const struct ble_gatt_error *error,
                                         const struct ble_gatt_svc *service, void *arg) {
  auto *impl = static_cast<BLEClient::Impl *>(arg);
  if (!impl) return 0;

  if (error->status == 0 && service != nullptr) {
    RemoteServiceEntry entry;
    if (service->uuid.u.type == BLE_UUID_TYPE_16) {
      entry.uuid = BLEUUID(static_cast<uint16_t>(BLE_UUID16(&service->uuid.u)->value));
    } else if (service->uuid.u.type == BLE_UUID_TYPE_32) {
      entry.uuid = BLEUUID(static_cast<uint32_t>(BLE_UUID32(&service->uuid.u)->value));
    } else {
      entry.uuid = BLEUUID(BLE_UUID128(&service->uuid.u)->value, 16, true);
    }
    entry.startHandle = service->start_handle;
    entry.endHandle = service->end_handle;
    impl->discoveredServices.push_back(entry);
    return 0;
  }

  if (error->status == BLE_HS_EDONE) {
    impl->discoverSync.give(BTStatus::OK);
  } else {
    impl->discoverSync.give(BTStatus::Fail);
  }
  return 0;
}

// --------------------------------------------------------------------------
// MTU exchange callback
// --------------------------------------------------------------------------

int BLEClient::Impl::mtuExchangeCb(uint16_t connHandle, const struct ble_gatt_error *error, uint16_t mtu, void *arg) {
  if (error->status == 0) {
    log_i("MTU exchange complete: %d", mtu);
  } else {
    log_w("MTU exchange failed, status=%d", error->status);
  }
  return 0;
}

// --------------------------------------------------------------------------
// BLEClass::createClient() -- NimBLE factory method
// --------------------------------------------------------------------------

BLEClient BLEClass::createClient() {
  if (!isInitialized()) {
    return BLEClient();
  }

  return BLEClient(std::make_shared<BLEClient::Impl>());
}

#endif /* BLE_NIMBLE */
