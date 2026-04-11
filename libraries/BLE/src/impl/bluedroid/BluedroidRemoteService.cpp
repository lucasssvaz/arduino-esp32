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

// --------------------------------------------------------------------------
// BLERemoteService -- Bluedroid backend
// --------------------------------------------------------------------------

BLERemoteService::BLERemoteService() : _impl(nullptr) {}
BLERemoteService::operator bool() const { return _impl != nullptr; }
BLERemoteCharacteristic BLERemoteService::getCharacteristic(const BLEUUID &) { return BLERemoteCharacteristic(); }
std::vector<BLERemoteCharacteristic> BLERemoteService::getCharacteristics() const { return {}; }
BLEClient BLERemoteService::getClient() const { return BLEClient(); }
BLEUUID BLERemoteService::getUUID() const { return BLEUUID(); }
uint16_t BLERemoteService::getHandle() const { return 0; }
String BLERemoteService::getValue(const BLEUUID &) { return ""; }
BTStatus BLERemoteService::setValue(const BLEUUID &, const String &) { return BTStatus::NotSupported; }
String BLERemoteService::toString() const { return "BLERemoteService(Bluedroid)"; }

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
