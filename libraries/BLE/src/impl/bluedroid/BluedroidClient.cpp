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
#include "BLEAdvertisedDevice.h"

#include "BluedroidClient.h"
#include "BluedroidRemoteTypes.h"
#include "BluedroidUUID.h"
#include "impl/BLEImplHelpers.h"
#include "impl/BLEConnInfoData.h"
#include "esp32-hal-log.h"

#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <esp_gatt_common_api.h>
#include <string.h>

// --------------------------------------------------------------------------
// BLEConnInfoImpl -- Bluedroid bridge (client-side)
// --------------------------------------------------------------------------

struct BLEConnInfoImpl {
  static BLEConnInfo make(uint16_t connId, const uint8_t bda[6], uint16_t mtu = 23) {
    BLEConnInfo info;
    info._valid = true;
    auto *d = info.data();
    d->handle = connId;
    d->address = BTAddress(bda, BTAddress::Type::Public);
    d->mtu = mtu;
    d->central = true;  // Client is central
    d->encrypted = false;
    d->authenticated = false;
    d->bonded = false;
    d->keySize = 0;
    d->interval = 0;
    d->latency = 0;
    d->supervisionTimeout = 0;
    d->txPhy = 1;
    d->rxPhy = 1;
    d->rssi = 0;
    return info;
  }

  static void setMTU(BLEConnInfo &info, uint16_t mtu) {
    if (info) info.data()->mtu = mtu;
  }
};

// --------------------------------------------------------------------------
// Static data
// --------------------------------------------------------------------------

uint16_t BLEClient::Impl::s_nextAppId = 0x10;  // Start from 0x10 to avoid collision with server
std::vector<BLEClient::Impl *> BLEClient::Impl::s_clients;

BLEClient::Impl::~Impl() {
  // Remove from static list
  for (auto it = s_clients.begin(); it != s_clients.end(); ++it) {
    if (*it == this) {
      s_clients.erase(it);
      break;
    }
  }
  // Unregister GATTC app if registered
  if (gattcIf != ESP_GATT_IF_NONE) {
    esp_ble_gattc_app_unregister(gattcIf);
    gattcIf = ESP_GATT_IF_NONE;
  }
  if (mtx) vSemaphoreDelete(mtx);
}

// --------------------------------------------------------------------------
// Callback dispatch helpers
// --------------------------------------------------------------------------

static void dispatchConnect(BLEClient::Impl *impl, const BLEConnInfo &conn) {
  BLEClient::ConnectHandler connectCb;
  BLEClient::Callbacks *cbs = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    connectCb = impl->onConnectCb;
    cbs = impl->callbacks;
  }
  BLEClient handle = BLEClient::Impl::makeHandle(impl);
  if (connectCb) connectCb(handle, conn);
  if (cbs) cbs->onConnect(handle, conn);
}

static void dispatchDisconnect(BLEClient::Impl *impl, const BLEConnInfo &conn, uint8_t reason) {
  BLEClient::DisconnectHandler disconnectCb;
  BLEClient::Callbacks *cbs = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    disconnectCb = impl->onDisconnectCb;
    cbs = impl->callbacks;
  }
  BLEClient handle = BLEClient::Impl::makeHandle(impl);
  if (disconnectCb) disconnectCb(handle, conn, reason);
  if (cbs) cbs->onDisconnect(handle, conn, reason);
}

static void dispatchConnectFail(BLEClient::Impl *impl, int reason) {
  BLEClient::ConnectFailHandler failCb;
  BLEClient::Callbacks *cbs = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    failCb = impl->onConnectFailCb;
    cbs = impl->callbacks;
  }
  BLEClient handle = BLEClient::Impl::makeHandle(impl);
  if (failCb) failCb(handle, reason);
  if (cbs) cbs->onConnectFail(handle, reason);
}

static void dispatchMtuChanged(BLEClient::Impl *impl, const BLEConnInfo &conn, uint16_t mtu) {
  BLEClient::MtuChangedHandler mtuCb;
  BLEClient::Callbacks *cbs = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    mtuCb = impl->onMtuChangedCb;
    cbs = impl->callbacks;
  }
  BLEClient handle = BLEClient::Impl::makeHandle(impl);
  if (mtuCb) mtuCb(handle, conn, mtu);
  if (cbs) cbs->onMtuChanged(handle, conn, mtu);
}

// --------------------------------------------------------------------------
// createClient
// --------------------------------------------------------------------------

BLEClient BLEClass::createClient() {
  if (!isInitialized()) {
    log_e("createClient: BLE not initialized");
    return BLEClient();
  }

  auto impl = std::make_shared<BLEClient::Impl>();
  impl->appId = BLEClient::Impl::s_nextAppId++;
  BLEClient::Impl::s_clients.push_back(impl.get());

  // Register GATTC application
  impl->regSync.take();
  esp_err_t err = esp_ble_gattc_app_register(impl->appId);
  if (err != ESP_OK) {
    log_e("esp_ble_gattc_app_register: %s", esp_err_to_name(err));
    // Remove from s_clients
    auto &clients = BLEClient::Impl::s_clients;
    for (auto it = clients.begin(); it != clients.end(); ++it) {
      if (*it == impl.get()) { clients.erase(it); break; }
    }
    return BLEClient();
  }

  BTStatus st = impl->regSync.wait(5000);
  if (!st) {
    log_e("GATTC app registration failed");
    auto &clients = BLEClient::Impl::s_clients;
    for (auto it = clients.begin(); it != clients.end(); ++it) {
      if (*it == impl.get()) { clients.erase(it); break; }
    }
    return BLEClient();
  }

  log_i("GATTC registered (gattc_if=%u, appId=%u)", impl->gattcIf, impl->appId);
  return BLEClient(impl);
}

// --------------------------------------------------------------------------
// connect / disconnect
// --------------------------------------------------------------------------

BTStatus BLEClient::connect(const BTAddress &address, uint32_t timeoutMs) {
  return connect(address, BLEPhy::PHY_1M, timeoutMs);
}

BTStatus BLEClient::connect(const BLEAdvertisedDevice &device, uint32_t timeoutMs) {
  return connect(device.getAddress(), BLEPhy::PHY_1M, timeoutMs);
}

BTStatus BLEClient::connect(const BTAddress &address, BLEPhy /*phy*/, uint32_t timeoutMs) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);

  if (impl.connected) {
    log_e("Client: already connected");
    return BTStatus::AlreadyConnected;
  }

  if (impl.gattcIf == ESP_GATT_IF_NONE) {
    log_e("Client: GATTC not registered");
    return BTStatus::InvalidState;
  }

  log_d("Client: connecting to %s (timeout=%u ms)", address.toString().c_str(), timeoutMs);
  impl.peerAddress = address;

  esp_bd_addr_t bda;
  memcpy(bda, address.data(), 6);

  impl.connectSync.take();
  esp_err_t err = esp_ble_gattc_open(
    impl.gattcIf,
    bda,
    static_cast<esp_ble_addr_type_t>(address.type()),
    true  // direct connection
  );

  if (err != ESP_OK) {
    log_e("esp_ble_gattc_open: %s", esp_err_to_name(err));
    impl.connectSync.give(BTStatus::Fail);
    return BTStatus::Fail;
  }

  BTStatus st = impl.connectSync.wait(timeoutMs);
  if (!st) {
    log_e("Connection failed or timed out: %d", static_cast<int>(st.code()));
    // Cancel pending connection
    esp_ble_gap_disconnect(bda);
    return st;
  }

  return BTStatus::OK;
}

BTStatus BLEClient::connect(const BLEAdvertisedDevice &device, BLEPhy phy, uint32_t timeoutMs) {
  return connect(device.getAddress(), phy, timeoutMs);
}

BTStatus BLEClient::connectAsync(const BTAddress &address, BLEPhy /*phy*/) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);

  if (impl.connected) {
    log_w("Client: connectAsync - already connected");
    return BTStatus::AlreadyConnected;
  }
  if (impl.gattcIf == ESP_GATT_IF_NONE) {
    log_e("Client: connectAsync - GATTC not registered");
    return BTStatus::InvalidState;
  }

  impl.peerAddress = address;
  esp_bd_addr_t bda;
  memcpy(bda, address.data(), 6);

  esp_err_t err = esp_ble_gattc_open(
    impl.gattcIf, bda,
    static_cast<esp_ble_addr_type_t>(address.type()),
    true
  );
  if (err != ESP_OK) {
    log_e("Client: esp_ble_gattc_open: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

BTStatus BLEClient::connectAsync(const BLEAdvertisedDevice &device, BLEPhy phy) {
  return connectAsync(device.getAddress(), phy);
}

BTStatus BLEClient::cancelConnect() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (impl.connected) {
    log_w("Client: cancelConnect - already connected, not cancelling");
    return BTStatus::AlreadyConnected;
  }

  esp_bd_addr_t bda;
  memcpy(bda, impl.peerAddress.data(), 6);
  esp_err_t err = esp_ble_gap_disconnect(bda);
  if (err != ESP_OK) {
    log_e("Client: cancelConnect esp_ble_gap_disconnect: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

BTStatus BLEClient::disconnect() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);

  if (!impl.connected) {
    log_w("Client: disconnect called but not connected");
    return BTStatus::NotConnected;
  }

  esp_err_t err = esp_ble_gattc_close(impl.gattcIf, impl.connId);
  if (err != ESP_OK) {
    log_e("esp_ble_gattc_close: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

// --------------------------------------------------------------------------
// Service discovery
// --------------------------------------------------------------------------

BTStatus BLEClient::discoverServices() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (!impl.connected) {
    log_w("Client: discoverServices called but not connected");
    return BTStatus::NotConnected;
  }

  {
    BLELockGuard lock(impl.mtx);
    impl.discoveredServices.clear();
  }

  impl.discoverSync.take();
  esp_err_t err = esp_ble_gattc_search_service(impl.gattcIf, impl.connId, NULL);
  if (err != ESP_OK) {
    log_e("esp_ble_gattc_search_service: %s", esp_err_to_name(err));
    impl.discoverSync.give(BTStatus::Fail);
    return BTStatus::Fail;
  }

  BTStatus st = impl.discoverSync.wait(10000);
  if (!st) {
    log_e("Service discovery failed or timed out");
    return st;
  }

  log_i("Discovered %u services", (unsigned)impl.discoveredServices.size());
  return BTStatus::OK;
}

BLERemoteService BLEClient::getService(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLERemoteService());

  // Auto-discover if not done
  if (impl.discoveredServices.empty() && impl.connected) {
    if (!discoverServices()) return BLERemoteService();
  }

  BLELockGuard lock(impl.mtx);
  for (auto &svc : impl.discoveredServices) {
    if (svc->uuid == uuid) {
      return BLERemoteService(std::shared_ptr<BLERemoteService::Impl>(svc));
    }
  }
  return BLERemoteService();
}

std::vector<BLERemoteService> BLEClient::getServices() const {
  std::vector<BLERemoteService> result;
  if (!_impl) return result;

  BLELockGuard lock(_impl->mtx);
  for (auto &svc : _impl->discoveredServices) {
    result.push_back(BLERemoteService(std::shared_ptr<BLERemoteService::Impl>(svc)));
  }
  return result;
}

// --------------------------------------------------------------------------
// Security
// --------------------------------------------------------------------------

BTStatus BLEClient::secureConnection() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (!impl.connected) {
    log_w("Client: secureConnection called but not connected");
    return BTStatus::NotConnected;
  }

  esp_bd_addr_t bda;
  memcpy(bda, impl.peerAddress.data(), 6);
  esp_err_t err = esp_ble_set_encryption(bda, ESP_BLE_SEC_ENCRYPT_MITM);
  if (err != ESP_OK) {
    log_e("Client: esp_ble_set_encryption: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

// --------------------------------------------------------------------------
// MTU
// --------------------------------------------------------------------------

void BLEClient::setMTU(uint16_t mtu) {
  if (!_impl) return;

  esp_ble_gatt_set_local_mtu(mtu);

  if (_impl->connected) {
    _impl->mtuSync.take();
    esp_err_t err = esp_ble_gattc_send_mtu_req(_impl->gattcIf, _impl->connId);
    if (err == ESP_OK) {
      _impl->mtuSync.wait(3000);
    }
  }
}

uint16_t BLEClient::getMTU() const {
  return _impl ? _impl->mtu : 23;
}

// --------------------------------------------------------------------------
// RSSI
// --------------------------------------------------------------------------

int8_t BLEClient::getRSSI() const {
  if (!_impl || !_impl->connected) return -128;

  esp_bd_addr_t bda;
  memcpy(bda, _impl->peerAddress.data(), 6);

  _impl->rssiSync.take();
  esp_err_t err = esp_ble_gap_read_rssi(bda);
  if (err != ESP_OK) {
    log_w("Client: getRSSI esp_ble_gap_read_rssi: %s", esp_err_to_name(err));
    return -128;
  }

  BTStatus st = _impl->rssiSync.wait(3000);
  if (!st) {
    log_w("Client: getRSSI timed out");
    return -128;
  }
  return _impl->lastRssi;
}

// --------------------------------------------------------------------------
// Connection info
// --------------------------------------------------------------------------

uint16_t BLEClient::getHandle() const {
  return _impl ? _impl->connId : 0xFFFF;
}

BLEConnInfo BLEClient::getConnection() const {
  if (!_impl || !_impl->connected) return BLEConnInfo();
  return BLEConnInfoImpl::make(_impl->connId, _impl->peerAddress.data(), _impl->mtu);
}

// --------------------------------------------------------------------------
// Connection parameters
// --------------------------------------------------------------------------

BTStatus BLEClient::updateConnParams(const BLEConnParams &params) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (!impl.connected) {
    log_w("Client: updateConnParams called but not connected");
    return BTStatus::NotConnected;
  }

  esp_ble_conn_update_params_t cp;
  memcpy(cp.bda, impl.peerAddress.data(), 6);
  cp.min_int = params.minInterval;
  cp.max_int = params.maxInterval;
  cp.latency = params.latency;
  cp.timeout = params.supervisionTimeout;
  esp_err_t err = esp_ble_gap_update_conn_params(&cp);
  if (err != ESP_OK) {
    log_e("Client: esp_ble_gap_update_conn_params: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

// --------------------------------------------------------------------------
// PHY (BLE 5.0 -- limited support on Bluedroid)
// --------------------------------------------------------------------------

BTStatus BLEClient::setPhy(BLEPhy /*txPhy*/, BLEPhy /*rxPhy*/) {
  log_w("%s not supported on Bluedroid", __func__);
  return BTStatus::NotSupported;
}

BTStatus BLEClient::getPhy(BLEPhy &txPhy, BLEPhy &rxPhy) const {
  txPhy = BLEPhy::PHY_1M;
  rxPhy = BLEPhy::PHY_1M;
  return BTStatus::OK;
}

BTStatus BLEClient::setDataLen(uint16_t /*txOctets*/, uint16_t /*txTime*/) {
  log_w("%s not supported on Bluedroid", __func__);
  return BTStatus::NotSupported;
}

// --------------------------------------------------------------------------
// handleGATTC -- static event handler
// --------------------------------------------------------------------------

void BLEClient::Impl::handleGATTC(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                    esp_ble_gattc_cb_param_t *param) {
  // REG_EVT: match by app_id
  if (event == ESP_GATTC_REG_EVT) {
    for (auto *c : s_clients) {
      if (param->reg.app_id == c->appId) {
        if (param->reg.status == ESP_GATT_OK) {
          c->gattcIf = gattc_if;
          c->regSync.give(BTStatus::OK);
        } else {
          log_e("GATTC REG failed: status=%d", param->reg.status);
          c->regSync.give(BTStatus::Fail);
        }
        return;
      }
    }
    return;
  }

  // Find the client instance by gattc_if
  Impl *client = nullptr;
  for (auto *c : s_clients) {
    if (c->gattcIf == gattc_if) {
      client = c;
      break;
    }
  }

  // For CONNECT_EVT, also try matching by BDA if gattc_if didn't match
  if (!client && event == ESP_GATTC_CONNECT_EVT) {
    for (auto *c : s_clients) {
      if (memcmp(c->peerAddress.data(), param->connect.remote_bda, 6) == 0) {
        client = c;
        break;
      }
    }
  }

  if (!client) return;

  switch (event) {
    case ESP_GATTC_CONNECT_EVT: {
      client->connId = param->connect.conn_id;
      client->connected = true;
      // Request MTU exchange after connection
      esp_ble_gattc_send_mtu_req(gattc_if, param->connect.conn_id);
      break;
    }

    case ESP_GATTC_OPEN_EVT: {
      if (param->open.status == ESP_GATT_OK) {
        client->connId = param->open.conn_id;
        client->connected = true;
        log_i("Client: connected, connId=%u", client->connId);
        BLEConnInfo conn = BLEConnInfoImpl::make(client->connId, param->open.remote_bda, client->mtu);
        dispatchConnect(client, conn);
        client->connectSync.give(BTStatus::OK);
      } else {
        log_e("Client: GATTC open failed: status=%d", param->open.status);
        dispatchConnectFail(client, param->open.status);
        client->connectSync.give(BTStatus::Fail);
      }
      break;
    }

    case ESP_GATTC_DISCONNECT_EVT: {
      if (param->disconnect.conn_id != client->connId) break;

      bool wasConnected = client->connected;
      client->connected = false;
      uint8_t reason = param->disconnect.reason;
      log_i("Client: disconnected, connId=%u reason=0x%02x", client->connId, reason);

      // Release any waiting syncs
      client->connectSync.give(BTStatus::Fail);
      client->discoverSync.give(BTStatus::Fail);
      client->readSync.give(BTStatus::Fail);
      client->writeSync.give(BTStatus::Fail);
      client->mtuSync.give(BTStatus::Fail);
      client->rssiSync.give(BTStatus::Fail);

      if (wasConnected) {
        BLEConnInfo conn = BLEConnInfoImpl::make(client->connId, param->disconnect.remote_bda, client->mtu);
        dispatchDisconnect(client, conn, reason);
      }

      client->connId = 0xFFFF;
      {
        BLELockGuard lock(client->mtx);
        client->discoveredServices.clear();
      }
      break;
    }

    case ESP_GATTC_CLOSE_EVT: {
      break;
    }

    case ESP_GATTC_SEARCH_RES_EVT: {
      auto svc = std::make_shared<BLERemoteService::Impl>();
      svc->uuid = espToUuid(param->search_res.srvc_id.uuid);
      svc->startHandle = param->search_res.start_handle;
      svc->endHandle = param->search_res.end_handle;
      svc->client = client;
      BLELockGuard lock(client->mtx);
      client->discoveredServices.push_back(svc);
      break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      client->discoverSync.give(
        param->search_cmpl.status == ESP_GATT_OK ? BTStatus::OK : BTStatus::Fail);
      break;
    }

    case ESP_GATTC_READ_CHAR_EVT:
    case ESP_GATTC_READ_DESCR_EVT: {
      if (param->read.status == ESP_GATT_OK) {
        client->readBuf.assign(param->read.value, param->read.value + param->read.value_len);
      } else {
        client->readBuf.clear();
      }
      client->readSync.give(
        param->read.status == ESP_GATT_OK ? BTStatus::OK : BTStatus::Fail);
      break;
    }

    case ESP_GATTC_WRITE_CHAR_EVT:
    case ESP_GATTC_WRITE_DESCR_EVT: {
      client->writeSync.give(
        param->write.status == ESP_GATT_OK ? BTStatus::OK : BTStatus::Fail);
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      // Notification registration complete -- no sync needed
      break;
    }

    case ESP_GATTC_NOTIFY_EVT: {
      // Dispatch to the correct characteristic's notify callback
      uint16_t handle = param->notify.handle;
      BLELockGuard lock(client->mtx);
      for (auto &svc : client->discoveredServices) {
        for (auto &chr : svc->characteristics) {
          if (chr->handle == handle && chr->notifyCb) {
            // Create a temporary BLERemoteCharacteristic handle
            auto shared = std::shared_ptr<BLERemoteCharacteristic::Impl>(chr.get(),
                          [](BLERemoteCharacteristic::Impl *) {});
            BLERemoteCharacteristic chrHandle(std::move(shared));
            chr->notifyCb(chrHandle, param->notify.value,
                          param->notify.value_len, param->notify.is_notify);
            return;
          }
        }
      }
      break;
    }

    case ESP_GATTC_CFG_MTU_EVT: {
      if (param->cfg_mtu.status == ESP_GATT_OK) {
        client->mtu = param->cfg_mtu.mtu;
        log_i("Client: MTU exchanged, mtu=%u connId=%u", client->mtu, client->connId);
      } else {
        log_w("Client: MTU exchange failed, status=%d", param->cfg_mtu.status);
      }
      client->mtuSync.give(
        param->cfg_mtu.status == ESP_GATT_OK ? BTStatus::OK : BTStatus::Fail);

      BLEConnInfo conn = BLEConnInfoImpl::make(client->connId, client->peerAddress.data(), client->mtu);
      dispatchMtuChanged(client, conn, client->mtu);
      break;
    }

    case ESP_GATTC_SRVC_CHG_EVT: {
      log_i("Client: service changed indication received");
      break;
    }

    default:
      break;
  }
}

// --------------------------------------------------------------------------
// handleGAP -- static GAP event handler (for RSSI etc.)
// --------------------------------------------------------------------------

void BLEClient::Impl::handleGAP(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (event == ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT) {
    if (param->read_rssi_cmpl.status == ESP_BT_STATUS_SUCCESS) {
      for (auto *c : s_clients) {
        if (memcmp(c->peerAddress.data(), param->read_rssi_cmpl.remote_addr, 6) == 0) {
          c->lastRssi = param->read_rssi_cmpl.rssi;
          c->rssiSync.give(BTStatus::OK);
          return;
        }
      }
    } else {
      // Give to all clients to avoid deadlock
      for (auto *c : s_clients) {
        c->rssiSync.give(BTStatus::Fail);
      }
    }
  }
}

#endif /* BLE_BLUEDROID */
