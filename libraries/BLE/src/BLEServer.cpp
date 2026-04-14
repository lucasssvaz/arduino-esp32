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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)

#include "BLEServer.h"
#include "impl/BLEImplHelpers.h"
#include "impl/BLEServerBackend.h"

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

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
