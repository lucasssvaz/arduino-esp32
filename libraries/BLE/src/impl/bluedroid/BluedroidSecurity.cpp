/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) && defined(CONFIG_BLUEDROID_ENABLED)

#include "BLE.h"

#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <esp_gap_ble_api.h>
#include <esp_random.h>

struct BLESecurity::Impl {
  IOCapability ioCap = NoInputNoOutput;
  bool bonding = true;
  bool mitm = false;
  bool secureConnection = false;
  uint32_t staticPassKey = 0;
  bool passKeySet = false;
  bool regenOnConnect = false;
  bool forceAuth = false;
  KeyDist initiatorKeys = KeyDist::EncKey | KeyDist::IdKey;
  KeyDist responderKeys = KeyDist::EncKey | KeyDist::IdKey;
  uint8_t keySize = 16;

  PassKeyRequestHandler passKeyRequestCb;
  PassKeyDisplayHandler passKeyDisplayCb;
  ConfirmPassKeyHandler confirmPassKeyCb;
  SecurityRequestHandler securityRequestCb;
  AuthorizationHandler authorizationCb;
  AuthCompleteHandler authCompleteCb;
  BondStoreOverflowHandler bondOverflowCb;
};

BLESecurity::BLESecurity() : _impl(nullptr) {}
BLESecurity::operator bool() const { return _impl != nullptr; }

void BLESecurity::setIOCapability(IOCapability cap) {
  BLE_CHECK_IMPL(); impl.ioCap = cap;
}

void BLESecurity::setAuthenticationMode(bool bond, bool mitm, bool sc) {
  BLE_CHECK_IMPL();
  impl.bonding = bond;
  impl.mitm = mitm;
  impl.secureConnection = sc;
}

void BLESecurity::setPassKey(bool isStatic, uint32_t pk) {
  BLE_CHECK_IMPL();
  impl.passKeySet = isStatic;
  impl.staticPassKey = pk;
}

void BLESecurity::setStaticPassKey(uint32_t pk) {
  BLE_CHECK_IMPL(); impl.staticPassKey = pk; impl.passKeySet = true;
}

void BLESecurity::setRandomPassKey() {
  BLE_CHECK_IMPL(); impl.staticPassKey = generateRandomPassKey(); impl.passKeySet = true;
}

uint32_t BLESecurity::getPassKey() const {
  return _impl ? _impl->staticPassKey : 0;
}

uint32_t BLESecurity::generateRandomPassKey() {
  return esp_random() % 1000000;
}

void BLESecurity::regenPassKeyOnConnect(bool enable) {
  BLE_CHECK_IMPL(); impl.regenOnConnect = enable;
}

void BLESecurity::setInitiatorKeys(KeyDist keys) { BLE_CHECK_IMPL(); impl.initiatorKeys = keys; }
void BLESecurity::setResponderKeys(KeyDist keys) { BLE_CHECK_IMPL(); impl.responderKeys = keys; }
void BLESecurity::setKeySize(uint8_t size) { BLE_CHECK_IMPL(); impl.keySize = size; }

void BLESecurity::setForceAuthentication(bool force) { BLE_CHECK_IMPL(); impl.forceAuth = force; }
bool BLESecurity::getForceAuthentication() const { return _impl ? _impl->forceAuth : false; }

BTStatus BLESecurity::onPassKeyRequest(PassKeyRequestHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.passKeyRequestCb = std::move(h); return BTStatus::OK; }
BTStatus BLESecurity::onPassKeyDisplay(PassKeyDisplayHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.passKeyDisplayCb = std::move(h); return BTStatus::OK; }
BTStatus BLESecurity::onConfirmPassKey(ConfirmPassKeyHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.confirmPassKeyCb = std::move(h); return BTStatus::OK; }
BTStatus BLESecurity::onSecurityRequest(SecurityRequestHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.securityRequestCb = std::move(h); return BTStatus::OK; }
BTStatus BLESecurity::onAuthorization(AuthorizationHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.authorizationCb = std::move(h); return BTStatus::OK; }
BTStatus BLESecurity::onAuthenticationComplete(AuthCompleteHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.authCompleteCb = std::move(h); return BTStatus::OK; }

BTStatus BLESecurity::onBondStoreOverflow(BondStoreOverflowHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.bondOverflowCb = std::move(h); return BTStatus::OK; }

BTStatus BLESecurity::deleteBond(const BTAddress &) { return BTStatus::NotSupported; }
BTStatus BLESecurity::deleteAllBonds() { return BTStatus::NotSupported; }
std::vector<BTAddress> BLESecurity::getBondedDevices() const { return {}; }

BTStatus BLESecurity::startSecurity(uint16_t) { return BTStatus::NotSupported; }
bool BLESecurity::waitForAuthenticationComplete(uint32_t) { return false; }
void BLESecurity::resetSecurity() {}

void BLESecurity::notifyAuthComplete(const BLEConnInfo &conn, bool success) {
  BLE_CHECK_IMPL();
  if (impl.authCompleteCb) impl.authCompleteCb(conn, success);
}

uint32_t BLESecurity::resolvePasskeyForDisplay(const BLEConnInfo &) { return _impl ? _impl->staticPassKey : 0; }
uint32_t BLESecurity::resolvePasskeyForInput(const BLEConnInfo &conn) {
  BLE_CHECK_IMPL(0);
  return impl.passKeyRequestCb ? impl.passKeyRequestCb(conn) : impl.staticPassKey;
}
bool BLESecurity::resolveNumericComparison(const BLEConnInfo &conn, uint32_t numcmp) {
  return (!_impl || !_impl->confirmPassKeyCb) ? true : _impl->confirmPassKeyCb(conn, numcmp);
}
bool BLESecurity::notifyBondOverflow(const BTAddress &oldest) {
  if (!_impl || !_impl->bondOverflowCb) return false;
  _impl->bondOverflowCb(oldest);
  return true;
}

BLESecurity BLEClass::getSecurity() {
  if (!isInitialized()) return BLESecurity();
  static std::shared_ptr<BLESecurity::Impl> secImpl;
  if (!secImpl) secImpl = std::make_shared<BLESecurity::Impl>();
  return BLESecurity(secImpl);
}

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
