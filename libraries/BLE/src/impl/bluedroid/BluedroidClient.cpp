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

#include "BluedroidClient.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <esp_gap_ble_api.h>

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
