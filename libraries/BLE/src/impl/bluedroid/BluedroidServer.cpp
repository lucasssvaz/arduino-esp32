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

#include "BluedroidServer.h"
#include "BluedroidService.h"
#include "BluedroidCharacteristic.h"
#include "impl/BLESync.h"
#include "impl/BLEImplHelpers.h"
#include "impl/BLEConnInfoData.h"
#include "impl/BLEMutex.h"
#include "esp32-hal-log.h"

#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <esp_bt_device.h>
#include <string.h>

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

namespace {

void uuidToEsp(const BLEUUID &uuid, esp_bt_uuid_t &out) {
  memset(&out, 0, sizeof(out));
  if (uuid.bitSize() == 16) {
    out.len = ESP_UUID_LEN_16;
    out.uuid.uuid16 = uuid.toUint16();
  } else if (uuid.bitSize() == 32) {
    out.len = ESP_UUID_LEN_32;
    out.uuid.uuid32 = uuid.toUint32();
  } else {
    out.len = ESP_UUID_LEN_128;
    // BLEUUID stores big-endian; Bluedroid expects little-endian
    const uint8_t *data = uuid.data();
    for (int i = 0; i < 16; i++) {
      out.uuid.uuid128[i] = data[15 - i];
    }
  }
}

esp_gatt_perm_t permToEsp(BLEPermission perm) {
  esp_gatt_perm_t result = 0;
  if (perm & BLEPermission::Read)              result |= ESP_GATT_PERM_READ;
  if (perm & BLEPermission::ReadEncrypted)     result |= ESP_GATT_PERM_READ_ENCRYPTED;
  if (perm & BLEPermission::ReadAuthenticated) result |= ESP_GATT_PERM_READ_ENC_MITM;
  if (perm & BLEPermission::Write)             result |= ESP_GATT_PERM_WRITE;
  if (perm & BLEPermission::WriteEncrypted)    result |= ESP_GATT_PERM_WRITE_ENCRYPTED;
  if (perm & BLEPermission::WriteAuthenticated)result |= ESP_GATT_PERM_WRITE_ENC_MITM;
  return result;
}

constexpr uint16_t CCCD_UUID16 = 0x2902;

} // namespace

// --------------------------------------------------------------------------
// BLEConnInfoImpl -- Bluedroid bridge
// --------------------------------------------------------------------------

struct BLEConnInfoImpl {
  static BLEConnInfo make(uint16_t connId, const uint8_t bda[6], uint16_t mtu = 23) {
    BLEConnInfo info;
    info._valid = true;
    auto *d = info.data();
    d->handle = connId;
    d->address = BTAddress(bda, BTAddress::Type::Public);
    d->mtu = mtu;
    d->central = false;
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

  static void setConnParams(BLEConnInfo &info, uint16_t interval,
                             uint16_t latency, uint16_t timeout) {
    if (!info) return;
    auto *d = info.data();
    d->interval = interval;
    d->latency = latency;
    d->supervisionTimeout = timeout;
  }
};

// --------------------------------------------------------------------------
// Static instance & makeHandle
// --------------------------------------------------------------------------

BLEServer::Impl *BLEServer::Impl::s_instance = nullptr;

BLEServer BLEServer::Impl::makeHandle(BLEServer::Impl *impl) {
  return BLEServer(std::shared_ptr<BLEServer::Impl>(impl, [](BLEServer::Impl *) {}));
}

// --------------------------------------------------------------------------
// Connection helpers
// --------------------------------------------------------------------------

void BLEServer::Impl::connSet(uint16_t connHandle, const BLEConnInfo &connInfo) {
  for (auto &entry : connections) {
    if (entry.first == connHandle) {
      entry.second = connInfo;
      return;
    }
  }
  connections.emplace_back(connHandle, connInfo);
}

void BLEServer::Impl::connErase(uint16_t connHandle) {
  for (auto it = connections.begin(); it != connections.end(); ++it) {
    if (it->first == connHandle) {
      connections.erase(it);
      return;
    }
  }
}

BLEConnInfo *BLEServer::Impl::connFind(uint16_t connHandle) {
  for (auto &entry : connections) {
    if (entry.first == connHandle) {
      return &entry.second;
    }
  }
  return nullptr;
}

// --------------------------------------------------------------------------
// Callback dispatch (snapshot under lock, invoke outside)
// --------------------------------------------------------------------------

namespace {

void dispatchConnect(BLEServer::Impl *impl, const BLEConnInfo &connInfo) {
  decltype(impl->onConnectCb) cb;
  BLEServer::Callbacks *callbacks = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    cb = impl->onConnectCb;
    callbacks = impl->callbacks;
  }
  BLEServer serverHandle = BLEServer::Impl::makeHandle(impl);
  if (callbacks) callbacks->onConnect(serverHandle, connInfo);
  if (cb) cb(serverHandle, connInfo);
}

void dispatchDisconnect(BLEServer::Impl *impl, const BLEConnInfo &connInfo, uint8_t reason) {
  decltype(impl->onDisconnectCb) cb;
  BLEServer::Callbacks *callbacks = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    cb = impl->onDisconnectCb;
    callbacks = impl->callbacks;
  }
  BLEServer serverHandle = BLEServer::Impl::makeHandle(impl);
  if (callbacks) callbacks->onDisconnect(serverHandle, connInfo, reason);
  if (cb) cb(serverHandle, connInfo, reason);
}

void dispatchMtuChanged(BLEServer::Impl *impl, const BLEConnInfo &connInfo, uint16_t mtu) {
  decltype(impl->onMtuChangedCb) cb;
  BLEServer::Callbacks *callbacks = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    cb = impl->onMtuChangedCb;
    callbacks = impl->callbacks;
  }
  BLEServer serverHandle = BLEServer::Impl::makeHandle(impl);
  if (callbacks) callbacks->onMtuChanged(serverHandle, connInfo, mtu);
  if (cb) cb(serverHandle, connInfo, mtu);
}

void dispatchConnParamsUpdate(BLEServer::Impl *impl, const BLEConnInfo &connInfo) {
  decltype(impl->onConnParamsCb) cb;
  BLEServer::Callbacks *callbacks = nullptr;
  {
    BLELockGuard lock(impl->mtx);
    cb = impl->onConnParamsCb;
    callbacks = impl->callbacks;
  }
  BLEServer serverHandle = BLEServer::Impl::makeHandle(impl);
  if (callbacks) callbacks->onConnParamsUpdate(serverHandle, connInfo);
  if (cb) cb(serverHandle, connInfo);
}

// Find a characteristic by attribute handle across all services
BLECharacteristic::Impl *findCharByHandle(BLEServer::Impl *impl, uint16_t handle) {
  for (auto &svc : impl->services) {
    for (auto &chr : svc->characteristics) {
      if (chr->handle == handle) return chr.get();
    }
  }
  return nullptr;
}

// Find a descriptor by attribute handle across all services
BLEDescriptor::Impl *findDescByHandle(BLEServer::Impl *impl, uint16_t handle) {
  for (auto &svc : impl->services) {
    for (auto &chr : svc->characteristics) {
      for (auto &desc : chr->descriptors) {
        if (desc->handle == handle) return desc.get();
      }
    }
  }
  return nullptr;
}

// Find the characteristic that owns a given descriptor handle
BLECharacteristic::Impl *findCharForDesc(BLEServer::Impl *impl, uint16_t descHandle) {
  for (auto &svc : impl->services) {
    for (auto &chr : svc->characteristics) {
      for (auto &desc : chr->descriptors) {
        if (desc->handle == descHandle) return chr.get();
      }
    }
  }
  return nullptr;
}

} // namespace

// --------------------------------------------------------------------------
// BLEServer public API -- Bluedroid backend
// --------------------------------------------------------------------------

void BLEServer::advertiseOnDisconnect(bool enable) {
  BLE_CHECK_IMPL(); impl.advertiseOnDisconnect = enable;
}

BLEService BLEServer::createService(const BLEUUID &uuid, uint32_t numHandles, uint8_t instId) {
  BLE_CHECK_IMPL(BLEService());
  BLELockGuard lock(impl.mtx);

  for (auto &svc : impl.services) {
    if (svc->uuid == uuid && svc->instId == instId) {
      return BLEService(svc);
    }
  }

  auto svc = std::make_shared<BLEService::Impl>();
  svc->uuid = uuid;
  svc->numHandles = numHandles;
  svc->instId = instId;
  svc->server = _impl.get();
  impl.services.push_back(svc);

  return BLEService(svc);
}

BLEService BLEServer::getService(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLEService());
  BLELockGuard lock(impl.mtx);
  for (auto &svc : impl.services) {
    if (svc->uuid == uuid) {
      return BLEService(svc);
    }
  }
  return BLEService();
}

std::vector<BLEService> BLEServer::getServices() const {
  std::vector<BLEService> result;
  BLE_CHECK_IMPL(result);
  BLELockGuard lock(impl.mtx);
  for (auto &svc : impl.services) {
    result.push_back(BLEService(svc));
  }
  return result;
}

void BLEServer::removeService(const BLEService &service) {
  if (!_impl || !service._impl) return;
  BLELockGuard lock(_impl->mtx);
  auto &svcs = _impl->services;
  for (auto it = svcs.begin(); it != svcs.end(); ++it) {
    if (it->get() == service._impl.get()) {
      svcs.erase(it);
      break;
    }
  }
}

BTStatus BLEServer::start() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (impl.started) return BTStatus::OK;

  for (auto &svc : impl.services) {
    // 1. Create service
    esp_gatt_srvc_id_t srvc_id = {};
    srvc_id.is_primary = true;
    srvc_id.id.inst_id = svc->instId;
    uuidToEsp(svc->uuid, srvc_id.id.uuid);

    impl.pendingHandle = &svc->handle;
    impl.createSync.take();
    esp_err_t err = esp_ble_gatts_create_service(impl.gattsIf, &srvc_id, svc->numHandles);
    if (err != ESP_OK) {
      log_e("esp_ble_gatts_create_service: %s", esp_err_to_name(err));
      impl.pendingHandle = nullptr;
      return BTStatus::Fail;
    }
    BTStatus st = impl.createSync.wait(5000);
    impl.pendingHandle = nullptr;
    if (st != BTStatus::OK) {
      log_e("Create service failed/timeout");
      return st;
    }

    // 2. Add characteristics
    for (auto &chr : svc->characteristics) {
      esp_bt_uuid_t char_uuid;
      uuidToEsp(chr->uuid, char_uuid);
      esp_gatt_perm_t perm = permToEsp(chr->permissions);
      esp_gatt_char_prop_t prop = static_cast<uint8_t>(chr->properties);

      impl.pendingHandle = &chr->handle;
      impl.createSync.take();
      err = esp_ble_gatts_add_char(svc->handle, &char_uuid, perm, prop, nullptr, nullptr);
      if (err != ESP_OK) {
        log_e("esp_ble_gatts_add_char: %s", esp_err_to_name(err));
        impl.pendingHandle = nullptr;
        return BTStatus::Fail;
      }
      st = impl.createSync.wait(5000);
      impl.pendingHandle = nullptr;
      if (st != BTStatus::OK) {
        log_e("Add char failed/timeout");
        return st;
      }

      // 3. Add descriptors for this characteristic
      for (auto &desc : chr->descriptors) {
        esp_bt_uuid_t desc_uuid;
        uuidToEsp(desc->uuid, desc_uuid);
        esp_gatt_perm_t descPerm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE;

        impl.pendingHandle = &desc->handle;
        impl.createSync.take();
        err = esp_ble_gatts_add_char_descr(svc->handle, &desc_uuid, descPerm, nullptr, nullptr);
        if (err != ESP_OK) {
          log_e("esp_ble_gatts_add_char_descr: %s", esp_err_to_name(err));
          impl.pendingHandle = nullptr;
          return BTStatus::Fail;
        }
        st = impl.createSync.wait(5000);
        impl.pendingHandle = nullptr;
        if (st != BTStatus::OK) {
          log_e("Add descr failed/timeout");
          return st;
        }
      }
    }

    // 4. Start service
    impl.createSync.take();
    err = esp_ble_gatts_start_service(svc->handle);
    if (err != ESP_OK) {
      log_e("esp_ble_gatts_start_service: %s", esp_err_to_name(err));
      return BTStatus::Fail;
    }
    st = impl.createSync.wait(5000);
    if (st != BTStatus::OK) {
      log_e("Start service failed/timeout");
      return st;
    }
    svc->started = true;
  }

  impl.started = true;
  return BTStatus::OK;
}

bool BLEServer::isStarted() const { return _impl && _impl->started; }

size_t BLEServer::getConnectedCount() const {
  BLE_CHECK_IMPL(0);
  BLELockGuard lock(impl.mtx);
  return impl.connections.size();
}

std::vector<BLEConnInfo> BLEServer::getConnections() const {
  std::vector<BLEConnInfo> result;
  BLE_CHECK_IMPL(result);
  BLELockGuard lock(impl.mtx);
  for (const auto &entry : impl.connections) result.push_back(entry.second);
  return result;
}

BTStatus BLEServer::disconnect(uint16_t connHandle, uint8_t /*reason*/) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  esp_err_t err = esp_ble_gatts_close(impl.gattsIf, connHandle);
  return (err == ESP_OK) ? BTStatus::OK : BTStatus::Fail;
}

BTStatus BLEServer::connect(const BTAddress &address) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  esp_bd_addr_t bda;
  memcpy(bda, address.data(), 6);
  impl.connectSync.take();
  esp_err_t err = esp_ble_gatts_open(impl.gattsIf, bda, true);
  if (err != ESP_OK) return BTStatus::Fail;
  return impl.connectSync.wait(10000);
}

uint16_t BLEServer::getPeerMTU(uint16_t connHandle) const {
  BLE_CHECK_IMPL(0);
  BLELockGuard lock(impl.mtx);
  BLEConnInfo *info = const_cast<BLEServer::Impl &>(impl).connFind(connHandle);
  return info ? info->getMTU() : 23;
}

BTStatus BLEServer::updateConnParams(uint16_t connHandle, const BLEConnParams &params) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);

  BTAddress addr;
  {
    BLELockGuard lock(impl.mtx);
    BLEConnInfo *info = impl.connFind(connHandle);
    if (!info) return BTStatus::InvalidState;
    addr = info->getAddress();
  }

  esp_ble_conn_update_params_t connParams = {};
  memcpy(connParams.bda, addr.data(), 6);
  connParams.min_int = params.minInterval;
  connParams.max_int = params.maxInterval;
  connParams.latency = params.latency;
  connParams.timeout = params.timeout;

  esp_err_t err = esp_ble_gap_update_conn_params(&connParams);
  return (err == ESP_OK) ? BTStatus::OK : BTStatus::Fail;
}

BTStatus BLEServer::setPhy(uint16_t /*connHandle*/, BLEPhy /*txPhy*/, BLEPhy /*rxPhy*/) {
  return BTStatus::NotSupported;
}

BTStatus BLEServer::getPhy(uint16_t /*connHandle*/, BLEPhy & /*txPhy*/, BLEPhy & /*rxPhy*/) const {
  return BTStatus::NotSupported;
}

BTStatus BLEServer::setDataLen(uint16_t /*connHandle*/, uint16_t /*txOctets*/, uint16_t /*txTime*/) {
  return BTStatus::NotSupported;
}

int BLEServer::handleGapEvent(void *event) { return 0; }

BLEAdvertising BLEServer::getAdvertising() { return BLE.getAdvertising(); }
BTStatus BLEServer::startAdvertising() { return BLE.startAdvertising(); }
BTStatus BLEServer::stopAdvertising() { return BLE.stopAdvertising(); }

// --------------------------------------------------------------------------
// handleGATTS -- dispatches ESP GATTS events to server, services & chars
// --------------------------------------------------------------------------

void BLEServer::Impl::handleGATTS(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                   esp_ble_gatts_cb_param_t *param) {
  auto *impl = s_instance;
  if (!impl) return;

  // REG_EVT: match by app_id (gatts_if not assigned yet)
  if (event == ESP_GATTS_REG_EVT) {
    if (param->reg.app_id == impl->appId) {
      if (param->reg.status == ESP_GATT_OK) {
        impl->gattsIf = gatts_if;
        impl->regSync.give(BTStatus::OK);
      } else {
        log_e("GATTS REG failed: status=%d", param->reg.status);
        impl->regSync.give(BTStatus::Fail);
      }
    }
    return;
  }

  // Ignore events for other GATTS interfaces
  if (gatts_if != ESP_GATT_IF_NONE && gatts_if != impl->gattsIf) return;

  switch (event) {

    // ----- Connection lifecycle -----

    case ESP_GATTS_CONNECT_EVT: {
      uint16_t connId = param->connect.conn_id;
      BLEConnInfo connInfo = BLEConnInfoImpl::make(connId, param->connect.remote_bda);
      {
        BLELockGuard lock(impl->mtx);
        impl->connSet(connId, connInfo);
      }
      dispatchConnect(impl, connInfo);
      break;
    }

    case ESP_GATTS_DISCONNECT_EVT: {
      uint16_t connId = param->disconnect.conn_id;
      uint8_t reason = static_cast<uint8_t>(param->disconnect.reason);
      BLEConnInfo connInfo = BLEConnInfoImpl::make(connId, param->disconnect.remote_bda);

      bool shouldAdvertise = false;
      {
        BLELockGuard lock(impl->mtx);
        // Clean up subscriber state for this connection
        for (auto &svc : impl->services) {
          for (auto &chr : svc->characteristics) {
            auto &subs = chr->subscribers;
            for (auto it = subs.begin(); it != subs.end(); ++it) {
              if (it->first == connId) {
                subs.erase(it);
                break;
              }
            }
          }
        }
        impl->connErase(connId);
        shouldAdvertise = impl->advertiseOnDisconnect;
      }
      dispatchDisconnect(impl, connInfo, reason);
      if (shouldAdvertise) {
        BLE.startAdvertising();
      }
      break;
    }

    case ESP_GATTS_MTU_EVT: {
      uint16_t connId = param->mtu.conn_id;
      uint16_t mtu = param->mtu.mtu;

      BLEConnInfo connInfo;
      {
        BLELockGuard lock(impl->mtx);
        BLEConnInfo *existing = impl->connFind(connId);
        if (existing) {
          BLEConnInfoImpl::setMTU(*existing, mtu);
          connInfo = *existing;
        }
      }
      if (connInfo) {
        dispatchMtuChanged(impl, connInfo, mtu);
      }
      break;
    }

    // ----- Service/characteristic registration (sequential from start()) -----

    case ESP_GATTS_CREATE_EVT: {
      if (param->create.status == ESP_GATT_OK && impl->pendingHandle) {
        *impl->pendingHandle = param->create.service_handle;
      }
      impl->createSync.give(
          param->create.status == ESP_GATT_OK ? BTStatus::OK : BTStatus::Fail);
      break;
    }

    case ESP_GATTS_ADD_CHAR_EVT: {
      if (param->add_char.status == ESP_GATT_OK && impl->pendingHandle) {
        *impl->pendingHandle = param->add_char.attr_handle;
      }
      impl->createSync.give(
          param->add_char.status == ESP_GATT_OK ? BTStatus::OK : BTStatus::Fail);
      break;
    }

    case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
      if (param->add_char_descr.status == ESP_GATT_OK && impl->pendingHandle) {
        *impl->pendingHandle = param->add_char_descr.attr_handle;
      }
      impl->createSync.give(
          param->add_char_descr.status == ESP_GATT_OK ? BTStatus::OK : BTStatus::Fail);
      break;
    }

    case ESP_GATTS_START_EVT: {
      impl->createSync.give(
          param->start.status == ESP_GATT_OK ? BTStatus::OK : BTStatus::Fail);
      break;
    }

    // ----- Read / Write dispatch -----

    case ESP_GATTS_READ_EVT: {
      uint16_t handle = param->read.handle;
      uint16_t connId = param->read.conn_id;
      uint32_t transId = param->read.trans_id;
      uint16_t offset = param->read.offset;

      BLECharacteristic::Impl *chr = findCharByHandle(impl, handle);
      BLEDescriptor::Impl *desc = chr ? nullptr : findDescByHandle(impl, handle);

      if (chr) {
        // Invoke user read callback to let them update the value
        if (chr->onReadCb) {
          BLEConnInfo connInfo;
          {
            BLELockGuard lock(impl->mtx);
            BLEConnInfo *ci = impl->connFind(connId);
            if (ci) connInfo = *ci;
          }
          auto chrHandle = BLECharacteristic(
              std::shared_ptr<BLECharacteristic::Impl>(chr, [](BLECharacteristic::Impl *) {}));
          chr->onReadCb(chrHandle, connInfo);
        }

        if (param->read.need_rsp) {
          esp_gatt_rsp_t rsp = {};
          rsp.attr_value.handle = handle;
          rsp.attr_value.offset = offset;
          size_t len = chr->value.size();
          if (offset < len) {
            size_t sendLen = len - offset;
            if (sendLen > ESP_GATT_MAX_ATTR_LEN) sendLen = ESP_GATT_MAX_ATTR_LEN;
            rsp.attr_value.len = sendLen;
            memcpy(rsp.attr_value.value, chr->value.data() + offset, sendLen);
          }
          esp_ble_gatts_send_response(gatts_if, connId, transId, ESP_GATT_OK, &rsp);
        }
      } else if (desc) {
        if (param->read.need_rsp) {
          esp_gatt_rsp_t rsp = {};
          rsp.attr_value.handle = handle;
          rsp.attr_value.offset = offset;
          size_t len = desc->value.size();
          if (offset < len) {
            size_t sendLen = len - offset;
            if (sendLen > ESP_GATT_MAX_ATTR_LEN) sendLen = ESP_GATT_MAX_ATTR_LEN;
            rsp.attr_value.len = sendLen;
            memcpy(rsp.attr_value.value, desc->value.data() + offset, sendLen);
          }
          esp_ble_gatts_send_response(gatts_if, connId, transId, ESP_GATT_OK, &rsp);
        }
      } else {
        if (param->read.need_rsp) {
          esp_ble_gatts_send_response(gatts_if, connId, transId, ESP_GATT_NOT_FOUND, nullptr);
        }
      }
      break;
    }

    case ESP_GATTS_WRITE_EVT: {
      uint16_t handle = param->write.handle;
      uint16_t connId = param->write.conn_id;
      uint32_t transId = param->write.trans_id;
      bool needRsp = param->write.need_rsp;
      bool isPrep = param->write.is_prep;

      // Prepare writes: acknowledge and return (full long-write support is future work)
      if (isPrep) {
        if (needRsp) {
          esp_gatt_rsp_t rsp = {};
          rsp.attr_value.handle = handle;
          rsp.attr_value.offset = param->write.offset;
          rsp.attr_value.len = param->write.len;
          if (param->write.len <= ESP_GATT_MAX_ATTR_LEN) {
            memcpy(rsp.attr_value.value, param->write.value, param->write.len);
          }
          esp_ble_gatts_send_response(gatts_if, connId, transId, ESP_GATT_OK, &rsp);
        }
        break;
      }

      BLECharacteristic::Impl *chr = findCharByHandle(impl, handle);
      BLEDescriptor::Impl *desc = chr ? nullptr : findDescByHandle(impl, handle);

      if (chr) {
        chr->value.assign(param->write.value, param->write.value + param->write.len);

        if (chr->onWriteCb) {
          BLEConnInfo connInfo;
          {
            BLELockGuard lock(impl->mtx);
            BLEConnInfo *ci = impl->connFind(connId);
            if (ci) connInfo = *ci;
          }
          auto chrHandle = BLECharacteristic(
              std::shared_ptr<BLECharacteristic::Impl>(chr, [](BLECharacteristic::Impl *) {}));
          chr->onWriteCb(chrHandle, connInfo);
        }
        if (needRsp) {
          esp_ble_gatts_send_response(gatts_if, connId, transId, ESP_GATT_OK, nullptr);
        }
      } else if (desc) {
        desc->value.assign(param->write.value, param->write.value + param->write.len);

        // Handle CCCD writes (subscription state)
        if (desc->uuid == BLEUUID(CCCD_UUID16) && param->write.len >= 2) {
          uint16_t subVal = param->write.value[0] | (param->write.value[1] << 8);
          BLECharacteristic::Impl *ownerChr = findCharForDesc(impl, handle);
          if (ownerChr) {
            BLECharacteristic::SubscribeHandler subCb;
            BLEConnInfo connInfo;
            {
              BLELockGuard lock(impl->mtx);
              if (subVal > 0) {
                bool found = false;
                for (auto &kv : ownerChr->subscribers) {
                  if (kv.first == connId) { kv.second = subVal; found = true; break; }
                }
                if (!found) ownerChr->subscribers.emplace_back(connId, subVal);
              } else {
                for (auto it = ownerChr->subscribers.begin(); it != ownerChr->subscribers.end(); ++it) {
                  if (it->first == connId) { ownerChr->subscribers.erase(it); break; }
                }
              }
              subCb = ownerChr->onSubscribeCb;
              BLEConnInfo *ci = impl->connFind(connId);
              if (ci) connInfo = *ci;
            }
            if (subCb) {
              auto chrHandle = BLECharacteristic(
                  std::shared_ptr<BLECharacteristic::Impl>(ownerChr, [](BLECharacteristic::Impl *) {}));
              subCb(chrHandle, connInfo, subVal);
            }
          }
        }
        if (needRsp) {
          esp_ble_gatts_send_response(gatts_if, connId, transId, ESP_GATT_OK, nullptr);
        }
      } else {
        if (needRsp) {
          esp_ble_gatts_send_response(gatts_if, connId, transId, ESP_GATT_NOT_FOUND, nullptr);
        }
      }
      break;
    }

    case ESP_GATTS_EXEC_WRITE_EVT: {
      esp_ble_gatts_send_response(gatts_if, param->exec_write.conn_id,
                                  param->exec_write.trans_id, ESP_GATT_OK, nullptr);
      break;
    }

    case ESP_GATTS_CONF_EVT: {
      if (param->conf.status != ESP_GATT_OK) {
        log_w("Indicate confirm failed, status=%d", param->conf.status);
      }
      break;
    }

    // ----- connect() blocking -----

    case ESP_GATTS_OPEN_EVT: {
      impl->connectSync.give(
          param->open.status == ESP_GATT_OK ? BTStatus::OK : BTStatus::Fail);
      break;
    }

    default:
      break;
  }
}

// --------------------------------------------------------------------------
// handleGAP -- connection parameter updates
// --------------------------------------------------------------------------

void BLEServer::Impl::handleGAP(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  auto *impl = s_instance;
  if (!impl) return;

  switch (event) {
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT: {
      if (param->update_conn_params.status != ESP_BT_STATUS_SUCCESS) break;

      BLEConnInfo connInfo;
      {
        BLELockGuard lock(impl->mtx);
        for (auto &entry : impl->connections) {
          if (memcmp(entry.second.getAddress().data(),
                     param->update_conn_params.bda, 6) == 0) {
            BLEConnInfoImpl::setConnParams(entry.second,
                                           param->update_conn_params.conn_int,
                                           param->update_conn_params.latency,
                                           param->update_conn_params.timeout);
            connInfo = entry.second;
            break;
          }
        }
      }
      if (connInfo) {
        dispatchConnParamsUpdate(impl, connInfo);
      }
      break;
    }
    default:
      break;
  }
}

// --------------------------------------------------------------------------
// BLEClass::createServer() -- Bluedroid factory
// --------------------------------------------------------------------------

BLEServer BLEClass::createServer() {
  if (!isInitialized()) return BLEServer();

  static std::shared_ptr<BLEServer::Impl> server;
  if (!server) {
    server = std::make_shared<BLEServer::Impl>();
    BLEServer::Impl::s_instance = server.get();

    server->regSync.take();
    esp_err_t err = esp_ble_gatts_app_register(server->appId);
    if (err != ESP_OK) {
      log_e("esp_ble_gatts_app_register: %s", esp_err_to_name(err));
      BLEServer::Impl::s_instance = nullptr;
      server.reset();
      return BLEServer();
    }
    BTStatus st = server->regSync.wait(5000);
    if (st != BTStatus::OK) {
      log_e("GATTS app registration failed/timeout");
      BLEServer::Impl::s_instance = nullptr;
      server.reset();
      return BLEServer();
    }
  }
  return BLEServer(server);
}

#endif /* BLE_BLUEDROID */
