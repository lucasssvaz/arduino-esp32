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

#include "impl/BLESync.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <esp_gattc_api.h>
#include <esp_gap_ble_api.h>

// --------------------------------------------------------------------------
// BLEClient::Impl -- Bluedroid backend
// --------------------------------------------------------------------------

struct BLEClient::Impl {
  uint16_t connId = 0xFFFF;
  BTAddress peerAddress;
  bool connected = false;
  esp_gatt_if_t gattcIf = ESP_GATT_IF_NONE;
  BLESync connectSync;

  BLEClient::ConnectHandler onConnectCb;
  BLEClient::DisconnectHandler onDisconnectCb;
  BLEClient::ConnectFailHandler onConnectFailCb;
  BLEClient::MtuChangedHandler onMtuChangedCb;
  BLEClient::ConnParamsReqHandler onConnParamsReqCb;
  BLEClient::IdentityHandler onIdentityCb;
};

BLEClient::BLEClient() : _impl(nullptr) {}
BLEClient::operator bool() const { return _impl != nullptr; }

BTStatus BLEClient::connect(const BTAddress &, uint32_t) { return BTStatus::NotSupported; }
BTStatus BLEClient::connect(const BLEAdvertisedDevice &, uint32_t) { return BTStatus::NotSupported; }
BTStatus BLEClient::connect(const BTAddress &, BLEPhy, uint32_t) { return BTStatus::NotSupported; }
BTStatus BLEClient::connect(const BLEAdvertisedDevice &, BLEPhy, uint32_t) { return BTStatus::NotSupported; }
BTStatus BLEClient::connectAsync(const BTAddress &, BLEPhy) { return BTStatus::NotSupported; }
BTStatus BLEClient::connectAsync(const BLEAdvertisedDevice &, BLEPhy) { return BTStatus::NotSupported; }
BTStatus BLEClient::cancelConnect() { return BTStatus::NotSupported; }
BTStatus BLEClient::disconnect() { return BTStatus::NotSupported; }
bool BLEClient::isConnected() const { return _impl && _impl->connected; }
BTStatus BLEClient::secureConnection() { return BTStatus::NotSupported; }

BLERemoteService BLEClient::getService(const BLEUUID &) { return BLERemoteService(); }
std::vector<BLERemoteService> BLEClient::getServices() const { return {}; }
BTStatus BLEClient::discoverServices() { return BTStatus::NotSupported; }
String BLEClient::getValue(const BLEUUID &, const BLEUUID &) { return ""; }
BTStatus BLEClient::setValue(const BLEUUID &, const BLEUUID &, const String &) { return BTStatus::NotSupported; }

BTStatus BLEClient::onConnect(ConnectHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.onConnectCb = std::move(h); return BTStatus::OK; }
BTStatus BLEClient::onDisconnect(DisconnectHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.onDisconnectCb = std::move(h); return BTStatus::OK; }
BTStatus BLEClient::onConnectFail(ConnectFailHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.onConnectFailCb = std::move(h); return BTStatus::OK; }
BTStatus BLEClient::onMtuChanged(MtuChangedHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.onMtuChangedCb = std::move(h); return BTStatus::OK; }
BTStatus BLEClient::onConnParamsUpdateRequest(ConnParamsReqHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.onConnParamsReqCb = std::move(h); return BTStatus::OK; }
BTStatus BLEClient::onIdentity(IdentityHandler h) { BLE_CHECK_IMPL(BTStatus::InvalidState); impl.onIdentityCb = std::move(h); return BTStatus::OK; }

void BLEClient::setMTU(uint16_t) {}
uint16_t BLEClient::getMTU() const { return 23; }
int8_t BLEClient::getRSSI() const { return -128; }
BTAddress BLEClient::getPeerAddress() const { return _impl ? _impl->peerAddress : BTAddress(); }
uint16_t BLEClient::getHandle() const { return _impl ? _impl->connId : 0xFFFF; }
BLEConnInfo BLEClient::getConnection() const { return BLEConnInfo(); }
BTStatus BLEClient::updateConnParams(const BLEConnParams &) { return BTStatus::NotSupported; }
BTStatus BLEClient::setPhy(BLEPhy, BLEPhy) { return BTStatus::NotSupported; }
BTStatus BLEClient::getPhy(BLEPhy &, BLEPhy &) const { return BTStatus::NotSupported; }
BTStatus BLEClient::setDataLen(uint16_t, uint16_t) { return BTStatus::NotSupported; }
String BLEClient::toString() const { return "BLEClient(Bluedroid)"; }

BLEClient BLEClass::createClient() {
  if (!isInitialized()) return BLEClient();
  return BLEClient(std::make_shared<BLEClient::Impl>());
}

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
