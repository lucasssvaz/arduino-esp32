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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) && defined(CONFIG_BLUEDROID_ENABLED)

#include "BLE.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-bt.h"
#include "esp32-hal-bt-mem.h"
#include "esp32-hal-log.h"

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <esp_gattc_api.h>
#include <esp_gatt_common_api.h>
#include <esp_bt_device.h>

// --------------------------------------------------------------------------
// BLEClass::Impl -- Bluedroid backend state
// --------------------------------------------------------------------------

struct BLEClass::Impl {
  bool initialized = false;
  bool memoryReleased = false;
  String deviceName;
  uint16_t localMTU = 23;
  std::vector<BTAddress> whiteList;
  BLEClass::RawEventHandler customGapHandler;
  BLEClass::RawEventHandler customGattcHandler;
  BLEClass::RawEventHandler customGattsHandler;

  static void gapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
  static void gattsCallback(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
  static void gattcCallback(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
};

void BLEClass::Impl::gapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  auto *impl = BLE._impl;
  if (!impl) return;

  if (impl->customGapHandler) {
    impl->customGapHandler(reinterpret_cast<void *>(&event), param);
  }
}

void BLEClass::Impl::gattsCallback(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
  auto *impl = BLE._impl;
  if (!impl) return;

  if (impl->customGattsHandler) {
    impl->customGattsHandler(reinterpret_cast<void *>(&event), param);
  }
}

void BLEClass::Impl::gattcCallback(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
  auto *impl = BLE._impl;
  if (!impl) return;

  if (impl->customGattcHandler) {
    impl->customGattcHandler(reinterpret_cast<void *>(&event), param);
  }
}

// --------------------------------------------------------------------------
// BLEClass lifecycle
// --------------------------------------------------------------------------

BLEClass::BLEClass() : _impl(new Impl()) {}

BLEClass::~BLEClass() {
  if (_impl && _impl->initialized) {
    end(false);
  }
  delete _impl;
  _impl = nullptr;
}

BTStatus BLEClass::begin(const String &deviceName) {
  if (_impl->initialized) {
    return BTStatus::OK;
  }

  if (_impl->memoryReleased) {
    log_e("Cannot reinitialize BLE: memory was permanently released by end(true)");
    return BTStatus::InvalidState;
  }

  log_i("Initializing BLE stack: Bluedroid");

  if (!btStart()) {
    log_e("btStart() failed");
    return BTStatus::Fail;
  }

  esp_err_t err = esp_bluedroid_init();
  if (err != ESP_OK) {
    log_e("esp_bluedroid_init: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  err = esp_bluedroid_enable();
  if (err != ESP_OK) {
    log_e("esp_bluedroid_enable: %s", esp_err_to_name(err));
    esp_bluedroid_deinit();
    return BTStatus::Fail;
  }

  err = esp_ble_gap_register_callback(Impl::gapCallback);
  if (err != ESP_OK) {
    log_e("esp_ble_gap_register_callback: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  err = esp_ble_gatts_register_callback(Impl::gattsCallback);
  if (err != ESP_OK) {
    log_e("esp_ble_gatts_register_callback: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  err = esp_ble_gattc_register_callback(Impl::gattcCallback);
  if (err != ESP_OK) {
    log_e("esp_ble_gattc_register_callback: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  _impl->deviceName = deviceName;
  if (deviceName.length() > 0) {
    esp_ble_gap_set_device_name(deviceName.c_str());
  }

  if (_impl->localMTU != 23) {
    esp_ble_gatt_set_local_mtu(_impl->localMTU);
  }

  _impl->initialized = true;
  return BTStatus::OK;
}

void BLEClass::end(bool releaseMemory) {
  if (!_impl->initialized) {
    return;
  }

  esp_bluedroid_disable();
  esp_bluedroid_deinit();

  esp_bt_controller_disable();
  esp_bt_controller_deinit();

  if (releaseMemory) {
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
    _impl->memoryReleased = true;
  }

  _impl->initialized = false;
}

bool BLEClass::isInitialized() const {
  return _impl && _impl->initialized;
}

// --------------------------------------------------------------------------
// Identity
// --------------------------------------------------------------------------

BTAddress BLEClass::getAddress() const {
  if (!_impl || !_impl->initialized) {
    return BTAddress();
  }
  const uint8_t *addr = esp_bt_dev_get_address();
  if (!addr) return BTAddress();
  return BTAddress(addr, BTAddress::Type::Public);
}

String BLEClass::getDeviceName() const {
  return _impl ? _impl->deviceName : "";
}

BTStatus BLEClass::setOwnAddressType(BTAddress::Type type) {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;
  esp_err_t err = esp_ble_gap_config_local_privacy(type != BTAddress::Type::Public);
  return (err == ESP_OK) ? BTStatus::OK : BTStatus::Fail;
}

BTStatus BLEClass::setOwnAddress(const BTAddress & /*addr*/) {
  return BTStatus::NotSupported;
}

// --------------------------------------------------------------------------
// Power
// --------------------------------------------------------------------------

void BLEClass::setPower(int8_t txPowerDbm) {
  if (!_impl || !_impl->initialized) return;
  esp_power_level_t level;
  if (txPowerDbm <= -12) level = ESP_PWR_LVL_N12;
  else if (txPowerDbm <= -9) level = ESP_PWR_LVL_N9;
  else if (txPowerDbm <= -6) level = ESP_PWR_LVL_N6;
  else if (txPowerDbm <= -3) level = ESP_PWR_LVL_N3;
  else if (txPowerDbm <= 0) level = ESP_PWR_LVL_N0;
  else if (txPowerDbm <= 3) level = ESP_PWR_LVL_P3;
  else if (txPowerDbm <= 6) level = ESP_PWR_LVL_P6;
  else level = ESP_PWR_LVL_P9;
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, level);
}

int8_t BLEClass::getPower() const {
  if (!_impl || !_impl->initialized) return -128;
  switch (esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_DEFAULT)) {
    case ESP_PWR_LVL_N12: return -12;
    case ESP_PWR_LVL_N9:  return -9;
    case ESP_PWR_LVL_N6:  return -6;
    case ESP_PWR_LVL_N3:  return -3;
    case ESP_PWR_LVL_N0:  return 0;
    case ESP_PWR_LVL_P3:  return 3;
    case ESP_PWR_LVL_P6:  return 6;
    case ESP_PWR_LVL_P9:  return 9;
    default:              return -128;
  }
}

// --------------------------------------------------------------------------
// MTU
// --------------------------------------------------------------------------

BTStatus BLEClass::setMTU(uint16_t mtu) {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;
  esp_err_t err = esp_ble_gatt_set_local_mtu(mtu);
  if (err != ESP_OK) return BTStatus::InvalidParam;
  _impl->localMTU = mtu;
  return BTStatus::OK;
}

uint16_t BLEClass::getMTU() const {
  return _impl ? _impl->localMTU : 23;
}

// --------------------------------------------------------------------------
// IRK (Bluedroid uses different bond storage API)
// --------------------------------------------------------------------------

bool BLEClass::getLocalIRK(uint8_t irk[16]) const {
  // Bluedroid doesn't expose the local IRK through its public API
  return false;
}

String BLEClass::getLocalIRKString() const { return ""; }
String BLEClass::getLocalIRKBase64() const { return ""; }

bool BLEClass::getPeerIRK(const BTAddress & /*peer*/, uint8_t /*irk*/[16]) const {
  return false;
}

String BLEClass::getPeerIRKString(const BTAddress & /*peer*/) const { return ""; }
String BLEClass::getPeerIRKBase64(const BTAddress & /*peer*/) const { return ""; }
String BLEClass::getPeerIRKReverse(const BTAddress & /*peer*/) const { return ""; }

// --------------------------------------------------------------------------
// Default PHY (BLE 5.0)
// --------------------------------------------------------------------------

BTStatus BLEClass::setDefaultPhy(BLEPhy txPhy, BLEPhy rxPhy) {
#if defined(SOC_BLE_50_SUPPORTED)
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;
  esp_ble_gap_set_prefered_default_le_phy(static_cast<uint8_t>(txPhy), static_cast<uint8_t>(rxPhy));
  return BTStatus::OK;
#else
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEClass::getDefaultPhy(BLEPhy & /*txPhy*/, BLEPhy & /*rxPhy*/) const {
  return BTStatus::NotSupported;
}

// --------------------------------------------------------------------------
// Whitelist
// --------------------------------------------------------------------------

BTStatus BLEClass::whiteListAdd(const BTAddress &address) {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;
  esp_bd_addr_t bda;
  memcpy(bda, address.data(), 6);
  esp_err_t err = esp_ble_gap_update_whitelist(true, bda, static_cast<esp_ble_wl_addr_type_t>(address.type()));
  if (err == ESP_OK) {
    _impl->whiteList.push_back(address);
    return BTStatus::OK;
  }
  return BTStatus::Fail;
}

BTStatus BLEClass::whiteListRemove(const BTAddress &address) {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;
  esp_bd_addr_t bda;
  memcpy(bda, address.data(), 6);
  esp_err_t err = esp_ble_gap_update_whitelist(false, bda, static_cast<esp_ble_wl_addr_type_t>(address.type()));
  if (err == ESP_OK) {
    for (auto it = _impl->whiteList.begin(); it != _impl->whiteList.end(); ++it) {
      if (*it == address) { _impl->whiteList.erase(it); break; }
    }
    return BTStatus::OK;
  }
  return BTStatus::Fail;
}

bool BLEClass::isOnWhiteList(const BTAddress &address) const {
  BLE_CHECK_IMPL(false);
  for (const auto &a : impl.whiteList) {
    if (a == address) return true;
  }
  return false;
}

// --------------------------------------------------------------------------
// Stack info
// --------------------------------------------------------------------------

BLEClass::Stack BLEClass::getStack() const { return Stack::Bluedroid; }
const char *BLEClass::getStackName() const { return "Bluedroid"; }
bool BLEClass::isHostedBLE() const { return false; }

BTStatus BLEClass::setPins(int8_t, int8_t, int8_t, int8_t, int8_t, int8_t, int8_t) {
  return BTStatus::NotSupported;
}

// --------------------------------------------------------------------------
// Custom event handlers
// --------------------------------------------------------------------------

BTStatus BLEClass::setCustomGapHandler(RawEventHandler handler) {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;
  _impl->customGapHandler = std::move(handler);
  return BTStatus::OK;
}

BTStatus BLEClass::setCustomGattcHandler(RawEventHandler handler) {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;
  _impl->customGattcHandler = std::move(handler);
  return BTStatus::OK;
}

BTStatus BLEClass::setCustomGattsHandler(RawEventHandler handler) {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;
  _impl->customGattsHandler = std::move(handler);
  return BTStatus::OK;
}

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
