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

#include "NimBLESecurity.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"
#include <esp_random.h>

#include <host/ble_gap.h>
#include <host/ble_store.h>

void BLESecurity::Impl::applyToHost() const {
  ble_hs_cfg.sm_io_cap = static_cast<uint8_t>(ioCap);
  ble_hs_cfg.sm_bonding = bonding ? 1 : 0;
  ble_hs_cfg.sm_mitm = mitm ? 1 : 0;
  ble_hs_cfg.sm_sc = sc ? 1 : 0;
  ble_hs_cfg.sm_our_key_dist = initKeyDist;
  ble_hs_cfg.sm_their_key_dist = respKeyDist;
}

// --------------------------------------------------------------------------
// Stack-specific BLESecurity methods
// --------------------------------------------------------------------------

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
  if (!_impl) {
    log_w("Security: deleteBond called with no security impl");
    return BTStatus::InvalidState;
  }
  ble_addr_t addr;
  addr.type = static_cast<uint8_t>(address.type());
  memcpy(addr.val, address.data(), 6);
  int rc = ble_gap_unpair(&addr);
  if (rc != 0) {
    log_e("Security: deleteBond failed for %s rc=%d", address.toString().c_str(), rc);
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

BTStatus BLESecurity::deleteAllBonds() {
  if (!_impl) {
    log_w("Security: deleteAllBonds called with no security impl");
    return BTStatus::InvalidState;
  }
  ble_store_clear();
  return BTStatus::OK;
}

BTStatus BLESecurity::startSecurity(uint16_t connHandle) {
  if (!_impl) {
    log_w("Security: startSecurity called with no security impl");
    return BTStatus::InvalidState;
  }
  int rc = ble_gap_security_initiate(connHandle);
  if (rc != 0) {
    log_e("Security: ble_gap_security_initiate handle=%u rc=%d", connHandle, rc);
    return BTStatus::Fail;
  }
  return BTStatus::OK;
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

#endif /* BLE_NIMBLE */
