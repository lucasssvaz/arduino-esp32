/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
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
#if BLE_ENABLED

#include "BLE.h"
#include "BLEServer.h"
#include "impl/BLEImplHelpers.h"
#include "impl/BLEServerBackend.h"
#if BLE_SERVER_BACKEND_AVAILABLE
#include "impl/BLEServiceBackend.h"
#endif

#include "impl/BLEMutex.h"

BLEServer::BLEServer() : _impl(nullptr) {}

BLEServer::operator bool() const {
  BLE_CHECK_IMPL(false);
  (void)impl;
  return true;
}

BTStatus BLEServer::onConnect(ConnectHandler handler) {
#if BLE_SERVER_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.onConnectCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEServer::onDisconnect(DisconnectHandler handler) {
#if BLE_SERVER_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.onDisconnectCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEServer::onMtuChanged(MtuChangedHandler handler) {
#if BLE_SERVER_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.onMtuChangedCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEServer::onConnParamsUpdate(ConnParamsHandler handler) {
#if BLE_SERVER_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.onConnParamsCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEServer::onIdentity(IdentityHandler handler) {
#if BLE_SERVER_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.onIdentityCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEServer::setCallbacks(Callbacks &callbacks) {
#if BLE_SERVER_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.callbacks = &callbacks;
  return BTStatus::OK;
#else
  (void)callbacks;
  return BTStatus::NotSupported;
#endif
}

void BLEServer::resetCallbacks() {
#if BLE_SERVER_BACKEND_AVAILABLE
  if (!_impl) return;
  auto &impl = *_impl;
  BLELockGuard lock(impl.mtx);
  impl.callbacks = nullptr;
#endif
}

#if BLE_SERVER_BACKEND_AVAILABLE

// --------------------------------------------------------------------------
// Connection helpers (shared between backends)
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

BLEServer BLEServer::Impl::makeHandle(BLEServer::Impl *impl) {
  return BLEServer(std::shared_ptr<BLEServer::Impl>(impl, [](BLEServer::Impl *) {}));
}

namespace ble_server_dispatch {

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

} // namespace ble_server_dispatch

// --------------------------------------------------------------------------
// BLEServer public API (stack-agnostic)
// --------------------------------------------------------------------------

void BLEServer::advertiseOnDisconnect(bool enable) {
  BLE_CHECK_IMPL();
  impl.advertiseOnDisconnect = enable;
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
  result.reserve(impl.services.size());
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
  for (const auto &entry : impl.connections) {
    result.push_back(entry.second);
  }
  return result;
}

BLEAdvertising BLEServer::getAdvertising() { return BLE.getAdvertising(); }
BTStatus BLEServer::startAdvertising() { return BLE.startAdvertising(); }
BTStatus BLEServer::stopAdvertising() { return BLE.stopAdvertising(); }

#endif /* BLE_SERVER_BACKEND_AVAILABLE */

#endif /* BLE_ENABLED */
