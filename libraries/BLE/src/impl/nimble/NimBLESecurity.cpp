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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if (defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)) && defined(CONFIG_NIMBLE_ENABLED)

#include "BLE.h"

#include "NimBLESecurity.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"
#include <esp_random.h>

#include <host/ble_gap.h>
#include <host/ble_store.h>

BLESecurity::BLESecurity() : _impl(nullptr) {}

BLESecurity::operator bool() const { return _impl != nullptr; }

void BLESecurity::Impl::applyToHost() const {
  ble_hs_cfg.sm_io_cap = static_cast<uint8_t>(ioCap);
  ble_hs_cfg.sm_bonding = bonding ? 1 : 0;
  ble_hs_cfg.sm_mitm = mitm ? 1 : 0;
  ble_hs_cfg.sm_sc = sc ? 1 : 0;
  ble_hs_cfg.sm_our_key_dist = initKeyDist;
  ble_hs_cfg.sm_their_key_dist = respKeyDist;
}

void BLESecurity::setIOCapability(IOCapability cap) {
  BLE_CHECK_IMPL();
  impl.ioCap = cap;
  impl.applyToHost();
}

void BLESecurity::setAuthenticationMode(bool bonding, bool mitm, bool secureConnection) {
  BLE_CHECK_IMPL();
  impl.bonding = bonding;
  impl.mitm = mitm;
  impl.sc = secureConnection;
  impl.applyToHost();
}

void BLESecurity::setPassKey(bool isStatic, uint32_t passkey) {
  BLE_CHECK_IMPL();
  impl.staticPassKey = isStatic;
  impl.passKey = isStatic ? (passkey % 1000000) : generateRandomPassKey();
}

void BLESecurity::setStaticPassKey(uint32_t passkey) { setPassKey(true, passkey); }
void BLESecurity::setRandomPassKey() { setPassKey(false); }

uint32_t BLESecurity::getPassKey() const { return _impl ? _impl->passKey : 0; }

uint32_t BLESecurity::generateRandomPassKey() {
  return esp_random() % 1000000;
}

void BLESecurity::regenPassKeyOnConnect(bool enable) {
  BLE_CHECK_IMPL();
  impl.regenOnConnect = enable;
}

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

void BLESecurity::setInitiatorKeys(KeyDist keys) {
  BLE_CHECK_IMPL();
  impl.initKeyDist = static_cast<uint8_t>(keys);
  impl.applyToHost();
}

void BLESecurity::setResponderKeys(KeyDist keys) {
  BLE_CHECK_IMPL();
  impl.respKeyDist = static_cast<uint8_t>(keys);
  impl.applyToHost();
}

void BLESecurity::setKeySize(uint8_t size) {
  BLE_CHECK_IMPL();
  impl.keySize = size;
}

void BLESecurity::setForceAuthentication(bool force) {
  BLE_CHECK_IMPL();
  impl.forceAuth = force;
}

bool BLESecurity::getForceAuthentication() const {
  return _impl ? _impl->forceAuth : false;
}

std::vector<BTAddress> BLESecurity::getBondedDevices() const {
  std::vector<BTAddress> result;
  if (!_impl) return result;

  struct ble_store_key_sec key = {};
  struct ble_store_value_sec value = {};
  key.idx = 0;

  while (ble_store_read_peer_sec(&key, &value) == 0) {
    result.push_back(BTAddress(value.peer_addr.val, static_cast<BTAddress::Type>(value.peer_addr.type)));
    key.idx++;
    if (key.idx > 100) break;
  }
  return result;
}

BTStatus BLESecurity::deleteBond(const BTAddress &address) {
  if (!_impl) return BTStatus::InvalidState;
  ble_addr_t addr;
  addr.type = static_cast<uint8_t>(address.type());
  memcpy(addr.val, address.data(), 6);
  int rc = ble_gap_unpair(&addr);
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
}

BTStatus BLESecurity::deleteAllBonds() {
  if (!_impl) return BTStatus::InvalidState;
  ble_store_clear();
  return BTStatus::OK;
}

BTStatus BLESecurity::onBondStoreOverflow(BondStoreOverflowHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.bondOverflowCb = handler;
  return BTStatus::OK;
}

BTStatus BLESecurity::startSecurity(uint16_t connHandle) {
  if (!_impl) return BTStatus::InvalidState;
  int rc = ble_gap_security_initiate(connHandle);
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
}

bool BLESecurity::waitForAuthenticationComplete(uint32_t timeoutMs) {
  BLE_CHECK_IMPL(false);
  impl.authSync.take();
  BTStatus status = impl.authSync.wait(timeoutMs);
  return status == BTStatus::OK;
}

void BLESecurity::resetSecurity() {
  BLE_CHECK_IMPL();
  impl.ioCap = NoInputNoOutput;
  impl.bonding = false;
  impl.mitm = false;
  impl.sc = true;
  impl.forceAuth = false;
  impl.applyToHost();
}

// --------------------------------------------------------------------------
// Stack event dispatch methods
// --------------------------------------------------------------------------

void BLESecurity::notifyAuthComplete(const BLEConnInfo &conn, bool success) {
  BLE_CHECK_IMPL();
  if (impl.authCompleteCb) {
    impl.authCompleteCb(conn, success);
  }
  impl.authSync.give(success ? BTStatus::OK : BTStatus::AuthFailed);
}

uint32_t BLESecurity::resolvePasskeyForDisplay(const BLEConnInfo &conn) {
  BLE_CHECK_IMPL(0);
  uint32_t pk = impl.passKey;
  if (impl.regenOnConnect) {
    pk = generateRandomPassKey();
    impl.passKey = pk;
  }
  if (impl.passKeyDisplayCb) {
    impl.passKeyDisplayCb(conn, pk);
  }
  return pk;
}

uint32_t BLESecurity::resolvePasskeyForInput(const BLEConnInfo &conn) {
  BLE_CHECK_IMPL(0);
  if (impl.passKeyRequestCb) {
    return impl.passKeyRequestCb(conn);
  }
  return impl.passKey;
}

bool BLESecurity::resolveNumericComparison(const BLEConnInfo &conn, uint32_t numcmp) {
  BLE_CHECK_IMPL(true);
  if (impl.confirmPassKeyCb) {
    return impl.confirmPassKeyCb(conn, numcmp);
  }
  return true;
}

bool BLESecurity::notifyBondOverflow(const BTAddress &oldest) {
  if (!_impl || !_impl->bondOverflowCb) return false;
  _impl->bondOverflowCb(oldest);
  return true;
}

// --------------------------------------------------------------------------
// BLEClass::getSecurity() factory
// --------------------------------------------------------------------------

BLESecurity BLEClass::getSecurity() {
  if (!isInitialized()) {
    return BLESecurity();
  }
  static std::shared_ptr<BLESecurity::Impl> secImpl;
  if (!secImpl) {
    secImpl = std::make_shared<BLESecurity::Impl>();
  }
  return BLESecurity(secImpl);
}

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
