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

#include "impl/BLEImplHelpers.h"
#include "esp32-hal-bt.h"
#include "esp32-hal-bt-mem.h"
#include "esp32-hal-log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <nvs_flash.h>

#if SOC_BLE_SUPPORTED
#include <esp_bt.h>
#endif

#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>

extern "C" void ble_store_config_init(void);
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <store/config/ble_store_config.h>
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
#include <host/ble_hs_pvcy.h>
#endif

#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
extern bool hostedInitBLE();
extern void hostedDeinitBLE();
extern bool hostedSetPins(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t rst);
#endif

// --------------------------------------------------------------------------
// BLEClass::Impl -- NimBLE backend state
// --------------------------------------------------------------------------

struct BLEClass::Impl {
  bool synced = false;
  uint16_t localMTU = BLE_ATT_MTU_DFLT;
  uint8_t ownAddrType = BLE_OWN_ADDR_PUBLIC;
  ble_gap_event_listener gapListener{};
  BLEClass::RawEventHandler customGapHandler = nullptr;

  static void hostTask(void *param) {
    log_i("NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
  }

  static void onReset(int reason) {
    auto *impl = BLE._impl;
    if (!impl || !impl->synced) {
      return;
    }
    impl->synced = false;
    log_i("NimBLE host reset, reason=%d", reason);
  }

  static void onSync() {
    auto *impl = BLE._impl;
    if (!impl) {
      return;
    }

    if (impl->synced) {
      return;
    }

    if (BLE._deviceName.length() > 0) {
      int rc = ble_svc_gap_device_name_set(BLE._deviceName.c_str());
      if (rc != 0) {
        log_e("ble_svc_gap_device_name_set: rc=%d", rc);
      }
    }

    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
      rc = ble_hs_util_ensure_addr(1);
    }
    if (rc != 0) {
      log_e("onSync: failed to ensure BLE address, rc=%d", rc);
      return;
    }

    rc = ble_hs_id_copy_addr(BLE_OWN_ADDR_PUBLIC, NULL, NULL);
    if (rc != 0) {
      log_d("No public address available, using random");
      impl->ownAddrType = BLE_OWN_ADDR_RANDOM;
    }

    ble_npl_time_delay(1);
    impl->synced = true;
  }

  static int onStoreStatus(struct ble_store_status_event *event, void *arg) {
    if (event->event_code == BLE_STORE_EVENT_FULL) {
      BLESecurity sec = BLE.getSecurity();
      if (sec) {
        struct ble_store_key_sec key = {};
        struct ble_store_value_sec value = {};
        key.idx = 0;
        if (ble_store_read_peer_sec(&key, &value) == 0) {
          BTAddress oldest(value.peer_addr.val, static_cast<BTAddress::Type>(value.peer_addr.type));
          if (sec.notifyBondOverflow(oldest)) return 0;
        }
      }
    }
    return ble_store_util_status_rr(event, arg);
  }
};

// --------------------------------------------------------------------------
// BLEClass lifecycle
// --------------------------------------------------------------------------

BLEClass::BLEClass() : _impl(new Impl()) {}

BLEClass::~BLEClass() {
  if (_initialized) {
    end(false);
  }
  delete _impl;
  _impl = nullptr;
}

BTStatus BLEClass::begin(const String &deviceName) {
  if (_initialized) {
    return BTStatus::OK;
  }

  if (_memoryReleased) {
    log_e("Cannot reinitialize BLE: memory was permanently released by end(true)");
    return BTStatus::InvalidState;
  }

  log_i("Initializing BLE stack: NimBLE");

#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
  if (!hostedInitBLE()) {
    log_e("Failed to initialize ESP-Hosted for BLE");
    return BTStatus::Fail;
  }
#endif

  esp_err_t err = nimble_port_init();
  if (err != ESP_OK) {
    log_e("nimble_port_init: rc=%d", err);
    return BTStatus::Fail;
  }

  ble_hs_cfg.reset_cb = Impl::onReset;
  ble_hs_cfg.sync_cb = Impl::onSync;
  ble_hs_cfg.store_status_cb = Impl::onStoreStatus;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 0;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
  ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ID;
#endif

  _deviceName = deviceName;

  ble_store_config_init();
  nimble_port_freertos_init(Impl::hostTask);

  constexpr int kSyncTimeoutMs = 5000;
  int waited = 0;
  while (!_impl->synced && waited < kSyncTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(10));
    waited += 10;
  }
  if (!_impl->synced) {
    log_e("BLE host sync timeout after %d ms", kSyncTimeoutMs);
    nimble_port_stop();
    nimble_port_deinit();
    return BTStatus::Timeout;
  }

  _initialized = true;
  vTaskDelay(200 / portTICK_PERIOD_MS);
  return BTStatus::OK;
}

void BLEClass::end(bool releaseMemory) {
  if (!_impl || !_initialized) {
    return;
  }

  ble_gap_adv_stop();
  ble_gap_disc_cancel();

  nimble_port_stop();
  nimble_port_deinit();

#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
  hostedDeinitBLE();
#endif

#if SOC_BLE_SUPPORTED && CONFIG_BT_CONTROLLER_ENABLED
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
#endif

  if (releaseMemory) {
#if SOC_BLE_SUPPORTED && CONFIG_BT_CONTROLLER_ENABLED
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
#endif
    _memoryReleased = true;
  }

  _impl->synced = false;
  _initialized = false;
}

// --------------------------------------------------------------------------
// Identity
// --------------------------------------------------------------------------

BTAddress BLEClass::getAddress() const {
  if (!_impl || !_initialized) {
    return BTAddress();
  }
  uint8_t addr[6];
  uint8_t type = _impl->ownAddrType;
  int rc = ble_hs_id_copy_addr(type & 1, addr, NULL);
  if (rc != 0) {
    return BTAddress();
  }
  return BTAddress(addr, static_cast<BTAddress::Type>(type));
}

BTStatus BLEClass::setOwnAddressType(BTAddress::Type type) {
  if (!_impl || !_initialized) {
    return BTStatus::NotInitialized;
  }
  uint8_t nimbleType = static_cast<uint8_t>(type);
  int rc = ble_hs_id_copy_addr(nimbleType & 1, NULL, NULL);
  if (rc != 0) {
    log_e("Unable to set address type %u, rc=%d", nimbleType, rc);
    return BTStatus::InvalidParam;
  }
  _impl->ownAddrType = nimbleType;

  if (nimbleType == BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT || nimbleType == BLE_OWN_ADDR_RPA_RANDOM_DEFAULT) {
#ifdef CONFIG_IDF_TARGET_ESP32
    _impl->ownAddrType = BLE_OWN_ADDR_RANDOM;
    ble_hs_pvcy_rpa_config(NIMBLE_HOST_ENABLE_RPA);
#endif
  } else {
#ifdef CONFIG_IDF_TARGET_ESP32
    ble_hs_pvcy_rpa_config(NIMBLE_HOST_DISABLE_PRIVACY);
#endif
  }
  return BTStatus::OK;
}

BTStatus BLEClass::setOwnAddress(const BTAddress &addr) {
  if (!_impl || !_initialized) {
    return BTStatus::NotInitialized;
  }
  int rc = ble_hs_id_set_rnd(addr.data());
  if (rc != 0) {
    log_e("Failed to set address, rc=%d", rc);
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

// --------------------------------------------------------------------------
// MTU
// --------------------------------------------------------------------------

BTStatus BLEClass::setMTU(uint16_t mtu) {
  if (!_impl || !_initialized) {
    return BTStatus::NotInitialized;
  }
  int rc = ble_att_set_preferred_mtu(mtu);
  if (rc != 0) {
    log_e("ble_att_set_preferred_mtu: rc=%d", rc);
    return BTStatus::InvalidParam;
  }
  _impl->localMTU = mtu;
  return BTStatus::OK;
}

uint16_t BLEClass::getMTU() const {
  return _impl ? _impl->localMTU : BLE_ATT_MTU_DFLT;
}

// --------------------------------------------------------------------------
// IRK
// --------------------------------------------------------------------------

bool BLEClass::getLocalIRK(uint8_t irk[16]) const {
  if (!_impl || !_initialized || !irk) {
    return false;
  }
  struct ble_store_key_sec key = {};
  struct ble_store_value_sec value = {};
  key.peer_addr = (ble_addr_t){0, {0}};
  key.idx = 0;
  int rc = ble_store_read_our_sec(&key, &value);
  if (rc != 0 || !value.irk_present) {
    return false;
  }
  memcpy(irk, value.irk, 16);
  return true;
}

String BLEClass::getLocalIRKString() const {
  uint8_t irk[16];
  if (!getLocalIRK(irk)) {
    return "";
  }
  char buf[33];
  for (int i = 0; i < 16; i++) {
    snprintf(buf + i * 2, 3, "%02x", irk[i]);
  }
  return String(buf);
}

String BLEClass::getLocalIRKBase64() const {
  uint8_t irk[16];
  if (!getLocalIRK(irk)) {
    return "";
  }
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char out[25];
  int j = 0;
  for (int i = 0; i < 16; i += 3) {
    uint32_t n = (static_cast<uint32_t>(irk[i]) << 16);
    if (i + 1 < 16) {
      n |= (static_cast<uint32_t>(irk[i + 1]) << 8);
    }
    if (i + 2 < 16) {
      n |= irk[i + 2];
    }
    out[j++] = b64[(n >> 18) & 0x3F];
    out[j++] = b64[(n >> 12) & 0x3F];
    out[j++] = (i + 1 < 16) ? b64[(n >> 6) & 0x3F] : '=';
    out[j++] = (i + 2 < 16) ? b64[n & 0x3F] : '=';
  }
  out[j] = '\0';
  return String(out);
}

bool BLEClass::getPeerIRK(const BTAddress &peer, uint8_t irk[16]) const {
  if (!_impl || !_initialized || !irk) {
    return false;
  }
  int numBonds = 0;
  struct ble_store_key_sec key = {};
  struct ble_store_value_sec value = {};
  key.idx = 0;

  while (ble_store_read_peer_sec(&key, &value) == 0) {
    BTAddress bondAddr(value.peer_addr.val, static_cast<BTAddress::Type>(value.peer_addr.type));
    if (bondAddr == peer && value.irk_present) {
      memcpy(irk, value.irk, 16);
      return true;
    }
    key.idx++;
    numBonds++;
    if (numBonds > 100) {
      break;
    }
  }
  return false;
}

String BLEClass::getPeerIRKString(const BTAddress &peer) const {
  uint8_t irk[16];
  if (!getPeerIRK(peer, irk)) {
    return "";
  }
  char buf[33];
  for (int i = 0; i < 16; i++) {
    snprintf(buf + i * 2, 3, "%02x", irk[i]);
  }
  return String(buf);
}

String BLEClass::getPeerIRKBase64(const BTAddress &peer) const {
  uint8_t irk[16];
  if (!getPeerIRK(peer, irk)) {
    return "";
  }
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char out[25];
  int j = 0;
  for (int i = 0; i < 16; i += 3) {
    uint32_t n = (static_cast<uint32_t>(irk[i]) << 16);
    if (i + 1 < 16) {
      n |= (static_cast<uint32_t>(irk[i + 1]) << 8);
    }
    if (i + 2 < 16) {
      n |= irk[i + 2];
    }
    out[j++] = b64[(n >> 18) & 0x3F];
    out[j++] = b64[(n >> 12) & 0x3F];
    out[j++] = (i + 1 < 16) ? b64[(n >> 6) & 0x3F] : '=';
    out[j++] = (i + 2 < 16) ? b64[n & 0x3F] : '=';
  }
  out[j] = '\0';
  return String(out);
}

String BLEClass::getPeerIRKReverse(const BTAddress &peer) const {
  uint8_t irk[16];
  if (!getPeerIRK(peer, irk)) {
    return "";
  }
  char buf[33];
  for (int i = 0; i < 16; i++) {
    snprintf(buf + i * 2, 3, "%02x", irk[15 - i]);
  }
  return String(buf);
}

// --------------------------------------------------------------------------
// Default PHY (BLE 5.0)
// --------------------------------------------------------------------------

BTStatus BLEClass::setDefaultPhy(BLEPhy txPhy, BLEPhy rxPhy) {
#if BLE5_SUPPORTED
  if (!_impl || !_initialized) {
    return BTStatus::NotInitialized;
  }
  uint8_t txMask = static_cast<uint8_t>(txPhy);
  uint8_t rxMask = static_cast<uint8_t>(rxPhy);
  int rc = ble_gap_set_prefered_default_le_phy(txMask, rxMask);
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
#else
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClass::getDefaultPhy(BLEPhy &txPhy, BLEPhy &rxPhy) const {
  // NimBLE doesn't provide a getter for default PHY preferences
  return BTStatus::NotSupported;
}

// --------------------------------------------------------------------------
// Whitelist
// --------------------------------------------------------------------------

BTStatus BLEClass::whiteListAdd(const BTAddress &address) {
  if (!_initialized) {
    return BTStatus::NotInitialized;
  }
  if (isOnWhiteList(address)) {
    return BTStatus::OK;
  }
  _whiteList.push_back(address);

  std::vector<ble_addr_t> addrs(_whiteList.size());
  for (size_t i = 0; i < _whiteList.size(); i++) {
    addrs[i].type = static_cast<uint8_t>(_whiteList[i].type());
    memcpy(addrs[i].val, _whiteList[i].data(), 6);
  }
  int rc = ble_gap_wl_set(addrs.data(), addrs.size());
  if (rc != 0) {
    log_e("Failed adding to whitelist, rc=%d", rc);
    _whiteList.pop_back();
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

BTStatus BLEClass::whiteListRemove(const BTAddress &address) {
  if (!_initialized) {
    return BTStatus::NotInitialized;
  }
  for (auto it = _whiteList.begin(); it != _whiteList.end(); ++it) {
    if (*it == address) {
      _whiteList.erase(it);
      std::vector<ble_addr_t> addrs(_whiteList.size());
      for (size_t i = 0; i < _whiteList.size(); i++) {
        addrs[i].type = static_cast<uint8_t>(_whiteList[i].type());
        memcpy(addrs[i].val, _whiteList[i].data(), 6);
      }
      int rc = ble_gap_wl_set(addrs.data(), addrs.size());
      if (rc != 0) {
        log_e("Failed removing from whitelist, rc=%d", rc);
        _whiteList.push_back(address);
        return BTStatus::Fail;
      }
      return BTStatus::OK;
    }
  }
  return BTStatus::NotFound;
}

// startAdvertising/stopAdvertising are in NimBLEAdvertising.cpp

// --------------------------------------------------------------------------
// Stack info
// --------------------------------------------------------------------------

BLEClass::Stack BLEClass::getStack() const {
  return Stack::NimBLE;
}

const char *BLEClass::getStackName() const {
  return "NimBLE";
}

bool BLEClass::isHostedBLE() const {
#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
  return true;
#else
  return false;
#endif
}

// --------------------------------------------------------------------------
// Hosted BLE pins
// --------------------------------------------------------------------------

BTStatus BLEClass::setPins(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t rst) {
#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
  return hostedSetPins(clk, cmd, d0, d1, d2, d3, rst) ? BTStatus::OK : BTStatus::Fail;
#else
  return BTStatus::NotSupported;
#endif
}

// --------------------------------------------------------------------------
// Custom event handlers
// --------------------------------------------------------------------------

BTStatus BLEClass::setCustomGapHandler(RawEventHandler handler) {
  if (!_impl || !_initialized) {
    return BTStatus::NotInitialized;
  }
  _impl->customGapHandler = handler;

  auto wrapper = [](struct ble_gap_event *event, void *arg) -> int {
    auto *impl = static_cast<BLEClass::Impl *>(arg);
    if (impl->customGapHandler) {
      return impl->customGapHandler(event, nullptr);
    }
    return 0;
  };

  int rc = ble_gap_event_listener_register(&_impl->gapListener, wrapper, _impl);
  if (rc == BLE_HS_EALREADY) {
    log_i("Already listening to GAP events");
  } else if (rc != 0) {
    log_e("ble_gap_event_listener_register: rc=%d", rc);
    return BTStatus::Fail;
  }
  return BTStatus::OK;
}

BTStatus BLEClass::setCustomGattcHandler(RawEventHandler /*handler*/) {
  return BTStatus::NotSupported;
}

BTStatus BLEClass::setCustomGattsHandler(RawEventHandler /*handler*/) {
  return BTStatus::NotSupported;
}

// Factory methods in their respective impl files:
// createServer() -> NimBLEServer.cpp, getAdvertising/startAdvertising/stopAdvertising -> NimBLEAdvertising.cpp
// getSecurity() -> NimBLESecurity.cpp, createClient() -> NimBLEClient.cpp, getScan() -> NimBLEScan.cpp

#endif /* BLE_NIMBLE */
