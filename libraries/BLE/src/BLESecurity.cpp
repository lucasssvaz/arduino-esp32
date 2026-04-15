/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
 * Copyright 2017 chegewara
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

#include "BLESecurity.h"
#include "impl/BLESecurityBackend.h"
#include "impl/BLEImplHelpers.h"

#include <esp_random.h>

// --------------------------------------------------------------------------
// Constructors / handle validity
// --------------------------------------------------------------------------

BLESecurity::BLESecurity() : _impl(nullptr) {}

BLESecurity::operator bool() const { return _impl != nullptr; }

// --------------------------------------------------------------------------
// Passkey helpers (stack-agnostic)
// --------------------------------------------------------------------------

uint32_t BLESecurity::generateRandomPassKey() {
  return esp_random() % 1000000;
}

void BLESecurity::regenPassKeyOnConnect(bool enable) {
  BLE_CHECK_IMPL();
  impl.regenOnConnect = enable;
}

// --------------------------------------------------------------------------
// Callback registration
// --------------------------------------------------------------------------

BTStatus BLESecurity::onPassKeyRequest(PassKeyRequestHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.passKeyRequestCb = handler;
  return BTStatus::OK;
}

BTStatus BLESecurity::onPassKeyDisplay(PassKeyDisplayHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.passKeyDisplayCb = handler;
  return BTStatus::OK;
}

BTStatus BLESecurity::onConfirmPassKey(ConfirmPassKeyHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.confirmPassKeyCb = handler;
  return BTStatus::OK;
}

BTStatus BLESecurity::onSecurityRequest(SecurityRequestHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.securityRequestCb = handler;
  return BTStatus::OK;
}

BTStatus BLESecurity::onAuthorization(AuthorizationHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.authorizationCb = handler;
  return BTStatus::OK;
}

BTStatus BLESecurity::onAuthenticationComplete(AuthCompleteHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.authCompleteCb = handler;
  return BTStatus::OK;
}

BTStatus BLESecurity::onBondStoreOverflow(BondStoreOverflowHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.bondOverflowCb = handler;
  return BTStatus::OK;
}

// --------------------------------------------------------------------------
// Key size / force-authentication
// --------------------------------------------------------------------------

void BLESecurity::setKeySize(uint8_t size) { BLE_CHECK_IMPL(); impl.keySize = size; }

void BLESecurity::setForceAuthentication(bool force) { BLE_CHECK_IMPL(); impl.forceAuth = force; }
bool BLESecurity::getForceAuthentication() const { return _impl ? _impl->forceAuth : false; }

// --------------------------------------------------------------------------
// Authentication synchronization
// --------------------------------------------------------------------------

bool BLESecurity::waitForAuthenticationComplete(uint32_t timeoutMs) {
  BLE_CHECK_IMPL(false);
  impl.authSync.take();
  BTStatus status = impl.authSync.wait(timeoutMs);
  return status == BTStatus::OK;
}

// --------------------------------------------------------------------------
// Stack event dispatch (shared between backends)
// --------------------------------------------------------------------------

void BLESecurity::notifyAuthComplete(const BLEConnInfo &conn, bool success) {
  BLE_CHECK_IMPL();
  if (impl.authCompleteCb) {
    impl.authCompleteCb(conn, success);
  }
  impl.authSync.give(success ? BTStatus::OK : BTStatus::AuthFailed);
}

bool BLESecurity::resolveNumericComparison(const BLEConnInfo &conn, uint32_t numcmp) {
  if (!_impl || !_impl->confirmPassKeyCb) return true;
  return _impl->confirmPassKeyCb(conn, numcmp);
}

bool BLESecurity::notifyBondOverflow(const BTAddress &oldest) {
  if (!_impl || !_impl->bondOverflowCb) return false;
  _impl->bondOverflowCb(oldest);
  return true;
}

#endif /* BLE_ENABLED */
