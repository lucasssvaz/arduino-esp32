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

#include "impl/BLEGuards.h"
#if BLE_BLUEDROID

#include "BLE.h"

#include "BluedroidSecurity.h"
#include "impl/BLEImplHelpers.h"
#include "impl/BLESync.h"
#include "impl/BLEConnInfoData.h"
#include "esp32-hal-log.h"

#include <esp_gap_ble_api.h>
#include <esp_random.h>
#include <string.h>

BLESecurity::Impl *BLESecurity::Impl::s_instance = nullptr;

// --------------------------------------------------------------------------
// BLEConnInfoImpl -- Bluedroid bridge (security-side)
// --------------------------------------------------------------------------

struct BLEConnInfoImpl {
  static BLEConnInfo make(const uint8_t bda[6]) {
    BLEConnInfo info;
    info._valid = true;
    auto *d = info.data();
    d->handle = 0;
    d->address = BTAddress(bda, BTAddress::Type::Public);
    d->mtu = 23;
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

  static void setAuthResult(BLEConnInfo &info, bool encrypted, bool authenticated, bool bonded) {
    auto *d = info.data();
    d->encrypted = encrypted;
    d->authenticated = authenticated;
    d->bonded = bonded;
  }
};

void BLESecurity::Impl::applySecurityParams() {
  esp_ble_auth_req_t authReq = ESP_LE_AUTH_NO_BOND;
  if (bonding) authReq = static_cast<esp_ble_auth_req_t>(authReq | ESP_LE_AUTH_BOND);
  if (mitm) authReq = static_cast<esp_ble_auth_req_t>(authReq | ESP_LE_AUTH_REQ_MITM);
  if (secureConnection) authReq = static_cast<esp_ble_auth_req_t>(authReq | ESP_LE_AUTH_REQ_SC_ONLY);

  esp_ble_io_cap_t espIoCap;
  switch (ioCap) {
    case DisplayOnly:      espIoCap = ESP_IO_CAP_OUT; break;
    case DisplayYesNo:     espIoCap = ESP_IO_CAP_IO; break;
    case KeyboardOnly:     espIoCap = ESP_IO_CAP_IN; break;
    case KeyboardDisplay:  espIoCap = ESP_IO_CAP_KBDISP; break;
    default:               espIoCap = ESP_IO_CAP_NONE; break;
  }

  uint8_t initKeyDist = 0;
  if (static_cast<uint8_t>(initiatorKeys) & static_cast<uint8_t>(KeyDist::EncKey))  initKeyDist |= ESP_BLE_ENC_KEY_MASK;
  if (static_cast<uint8_t>(initiatorKeys) & static_cast<uint8_t>(KeyDist::IdKey))   initKeyDist |= ESP_BLE_ID_KEY_MASK;
  if (static_cast<uint8_t>(initiatorKeys) & static_cast<uint8_t>(KeyDist::SignKey)) initKeyDist |= ESP_BLE_CSR_KEY_MASK;
  if (static_cast<uint8_t>(initiatorKeys) & static_cast<uint8_t>(KeyDist::LinkKey)) initKeyDist |= ESP_BLE_LINK_KEY_MASK;

  uint8_t rspKeyDist = 0;
  if (static_cast<uint8_t>(responderKeys) & static_cast<uint8_t>(KeyDist::EncKey))  rspKeyDist |= ESP_BLE_ENC_KEY_MASK;
  if (static_cast<uint8_t>(responderKeys) & static_cast<uint8_t>(KeyDist::IdKey))   rspKeyDist |= ESP_BLE_ID_KEY_MASK;
  if (static_cast<uint8_t>(responderKeys) & static_cast<uint8_t>(KeyDist::SignKey)) rspKeyDist |= ESP_BLE_CSR_KEY_MASK;
  if (static_cast<uint8_t>(responderKeys) & static_cast<uint8_t>(KeyDist::LinkKey)) rspKeyDist |= ESP_BLE_LINK_KEY_MASK;

  if (passKeySet) {
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &staticPassKey, sizeof(uint32_t));
  }

  esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &authReq, sizeof(authReq));
  esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &espIoCap, sizeof(espIoCap));
  esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &keySize, sizeof(keySize));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &initKeyDist, sizeof(initKeyDist));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rspKeyDist, sizeof(rspKeyDist));
}

void BLESecurity::Impl::handleGAP(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  Impl *sec = s_instance;
  if (!sec) return;

  switch (event) {
    case ESP_GAP_BLE_PASSKEY_REQ_EVT: {
      BLEConnInfo conn = BLEConnInfoImpl::make(param->ble_security.ble_req.bd_addr);
      uint32_t pk = sec->staticPassKey;
      if (sec->passKeyRequestCb) {
        pk = sec->passKeyRequestCb(conn);
      }
      esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, true, pk);
      break;
    }

    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT: {
      BLEConnInfo conn = BLEConnInfoImpl::make(param->ble_security.key_notif.bd_addr);
      if (sec->passKeyDisplayCb) {
        sec->passKeyDisplayCb(conn, param->ble_security.key_notif.passkey);
      }
      break;
    }

    case ESP_GAP_BLE_NC_REQ_EVT: {
      BLEConnInfo conn = BLEConnInfoImpl::make(param->ble_security.key_notif.bd_addr);
      bool accept = true;
      if (sec->confirmPassKeyCb) {
        accept = sec->confirmPassKeyCb(conn, param->ble_security.key_notif.passkey);
      }
      esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, accept);
      break;
    }

    case ESP_GAP_BLE_SEC_REQ_EVT: {
      BLEConnInfo conn = BLEConnInfoImpl::make(param->ble_security.ble_req.bd_addr);
      bool accept = true;
      if (sec->securityRequestCb) {
        accept = sec->securityRequestCb(conn);
      }
      esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, accept);
      break;
    }

    case ESP_GAP_BLE_AUTH_CMPL_EVT: {
      BLEConnInfo conn = BLEConnInfoImpl::make(param->ble_security.auth_cmpl.bd_addr);
      bool authed = param->ble_security.auth_cmpl.auth_mode != ESP_LE_AUTH_NO_BOND;
      BLEConnInfoImpl::setAuthResult(conn, true, authed, authed);

      bool success = param->ble_security.auth_cmpl.success;
      if (sec->authCompleteCb) {
        sec->authCompleteCb(conn, success);
      }
      sec->authSync.give(success ? BTStatus::OK : BTStatus::AuthFailed);
      break;
    }

    case ESP_GAP_BLE_KEY_EVT: {
      log_d("BLE key type: %d", param->ble_security.ble_key.key_type);
      break;
    }

    default:
      break;
  }
}

// --------------------------------------------------------------------------
// Stack-specific BLESecurity methods
// --------------------------------------------------------------------------

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

void BLESecurity::setInitiatorKeys(KeyDist keys) { BLE_CHECK_IMPL(); impl.initiatorKeys = keys; }
void BLESecurity::setResponderKeys(KeyDist keys) { BLE_CHECK_IMPL(); impl.responderKeys = keys; }

BTStatus BLESecurity::deleteBond(const BTAddress &address) {
  if (!_impl) {
    log_w("Security: deleteBond called with no security impl");
    return BTStatus::InvalidState;
  }
  esp_bd_addr_t bda;
  memcpy(bda, address.data(), 6);
  esp_err_t err = esp_ble_remove_bond_device(bda);
  if (err != ESP_OK) {
    log_e("Security: esp_ble_remove_bond_device for %s: %s", address.toString().c_str(), esp_err_to_name(err));
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

BTStatus BLESecurity::deleteAllBonds() {
  if (!_impl) {
    log_w("Security: deleteAllBonds called with no security impl");
    return BTStatus::InvalidState;
  }

  int num = esp_ble_get_bond_device_num();
  if (num <= 0) return BTStatus::OK;

  std::vector<esp_ble_bond_dev_t> devList(num);
  esp_err_t err = esp_ble_get_bond_device_list(&num, devList.data());
  if (err != ESP_OK) {
    log_e("Security: esp_ble_get_bond_device_list: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  bool allOk = true;
  for (int i = 0; i < num; i++) {
    if (esp_ble_remove_bond_device(devList[i].bd_addr) != ESP_OK) {
      log_w("Security: failed to remove bond device %d of %d", i + 1, num);
      allOk = false;
    }
  }
  return allOk ? BTStatus::OK : BTStatus::Fail;
}

std::vector<BTAddress> BLESecurity::getBondedDevices() const {
  std::vector<BTAddress> result;
  if (!_impl) return result;

  int num = esp_ble_get_bond_device_num();
  if (num <= 0) return result;

  std::vector<esp_ble_bond_dev_t> devList(num);
  esp_err_t err = esp_ble_get_bond_device_list(&num, devList.data());
  if (err != ESP_OK) return result;

  for (int i = 0; i < num; i++) {
    result.push_back(BTAddress(devList[i].bd_addr, BTAddress::Type::Public));
  }
  return result;
}

BTStatus BLESecurity::startSecurity(uint16_t /*connHandle*/) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.applySecurityParams();
  return BTStatus::OK;
}

void BLESecurity::resetSecurity() {
  BLE_CHECK_IMPL();
  impl.ioCap = NoInputNoOutput;
  impl.bonding = true;
  impl.mitm = false;
  impl.secureConnection = false;
  impl.forceAuth = false;
  impl.applySecurityParams();
}

uint32_t BLESecurity::resolvePasskeyForDisplay(const BLEConnInfo &) {
  BLE_CHECK_IMPL(0);
  uint32_t pk = impl.staticPassKey;
  if (impl.regenOnConnect) {
    pk = generateRandomPassKey();
    impl.staticPassKey = pk;
  }
  return pk;
}

uint32_t BLESecurity::resolvePasskeyForInput(const BLEConnInfo &conn) {
  BLE_CHECK_IMPL(0);
  return impl.passKeyRequestCb ? impl.passKeyRequestCb(conn) : impl.staticPassKey;
}

// --------------------------------------------------------------------------
// BLEClass::getSecurity() factory
// --------------------------------------------------------------------------

BLESecurity BLEClass::getSecurity() {
  if (!isInitialized()) return BLESecurity();
  static std::shared_ptr<BLESecurity::Impl> secImpl;
  if (!secImpl) {
    secImpl = std::make_shared<BLESecurity::Impl>();
    BLESecurity::Impl::s_instance = secImpl.get();
    secImpl->applySecurityParams();
  }
  return BLESecurity(secImpl);
}

// --------------------------------------------------------------------------
// Free function for GAP security event routing from BluedroidCore.cpp
// --------------------------------------------------------------------------

void bluedroidSecurityHandleGAP(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  BLESecurity::Impl::handleGAP(event, param);
}

#endif /* BLE_BLUEDROID */
