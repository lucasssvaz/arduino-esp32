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

#include "BLEClient.h"
#include "BLERemoteService.h"
#include "impl/BLEImplHelpers.h"
#include "impl/BLEClientBackend.h"

#include "impl/BLEMutex.h"

BLEClient::BLEClient() : _impl(nullptr) {}

BLEClient::operator bool() const {
  BLE_CHECK_IMPL(false);
  (void)impl;
  return true;
}

BTStatus BLEClient::onConnect(ConnectHandler handler) {
#if BLE_CLIENT_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.onConnectCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClient::onDisconnect(DisconnectHandler handler) {
#if BLE_CLIENT_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.onDisconnectCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClient::onConnectFail(ConnectFailHandler handler) {
#if BLE_CLIENT_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.onConnectFailCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClient::onMtuChanged(MtuChangedHandler handler) {
#if BLE_CLIENT_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.onMtuChangedCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClient::onConnParamsUpdateRequest(ConnParamsReqHandler handler) {
#if BLE_CLIENT_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.onConnParamsReqCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClient::onIdentity(IdentityHandler handler) {
#if BLE_CLIENT_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.onIdentityCb = handler;
  return BTStatus::OK;
#else
  (void)handler;
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClient::setCallbacks(Callbacks &callbacks) {
#if BLE_CLIENT_BACKEND_AVAILABLE
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  BLELockGuard lock(impl.mtx);
  impl.callbacks = &callbacks;
  return BTStatus::OK;
#else
  (void)callbacks;
  return BTStatus::NotSupported;
#endif
}

void BLEClient::resetCallbacks() {
#if BLE_CLIENT_BACKEND_AVAILABLE
  if (!_impl) return;
  auto &impl = *_impl;
  BLELockGuard lock(impl.mtx);
  impl.callbacks = nullptr;
#endif
}

#if BLE_CLIENT_BACKEND_AVAILABLE

BLEClient BLEClient::Impl::makeHandle(BLEClient::Impl *impl) {
  return BLEClient(std::shared_ptr<BLEClient::Impl>(impl, [](BLEClient::Impl *){}));
}

BTAddress BLEClient::getPeerAddress() const {
  return _impl ? _impl->peerAddress : BTAddress();
}

bool BLEClient::isConnected() const {
  return _impl && _impl->connected;
}

String BLEClient::getValue(const BLEUUID &serviceUUID, const BLEUUID &charUUID) {
  BLERemoteService svc = getService(serviceUUID);
  if (!svc) return "";
  return svc.getValue(charUUID);
}

BTStatus BLEClient::setValue(const BLEUUID &serviceUUID, const BLEUUID &charUUID, const String &value) {
  BLERemoteService svc = getService(serviceUUID);
  if (!svc) return BTStatus::NotFound;
  return svc.setValue(charUUID, value);
}

String BLEClient::toString() const {
  if (!_impl) return "BLEClient(null)";
  String s = "BLEClient(peer=";
  s += _impl->peerAddress.toString();
  s += _impl->connected ? ", connected)" : ", disconnected)";
  return s;
}

#endif /* BLE_CLIENT_BACKEND_AVAILABLE */

#endif /* BLE_ENABLED */
