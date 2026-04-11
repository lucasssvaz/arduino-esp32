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
// BLERemoteCharacteristic -- Bluedroid backend
// --------------------------------------------------------------------------

BLERemoteCharacteristic::BLERemoteCharacteristic() : _impl(nullptr) {}
BLERemoteCharacteristic::operator bool() const { return _impl != nullptr; }

String BLERemoteCharacteristic::readValue(uint32_t) { return ""; }
uint8_t BLERemoteCharacteristic::readUInt8(uint32_t) { return 0; }
uint16_t BLERemoteCharacteristic::readUInt16(uint32_t) { return 0; }
uint32_t BLERemoteCharacteristic::readUInt32(uint32_t) { return 0; }
float BLERemoteCharacteristic::readFloat(uint32_t) { return 0.0f; }
size_t BLERemoteCharacteristic::readValue(uint8_t *, size_t, uint32_t) { return 0; }
const uint8_t *BLERemoteCharacteristic::readRawData(size_t *len) { if (len) *len = 0; return nullptr; }
BTStatus BLERemoteCharacteristic::writeValue(const uint8_t *, size_t, bool) { return BTStatus::NotSupported; }
BTStatus BLERemoteCharacteristic::writeValue(const String &, bool) { return BTStatus::NotSupported; }
BTStatus BLERemoteCharacteristic::writeValue(uint8_t, bool) { return BTStatus::NotSupported; }
bool BLERemoteCharacteristic::canRead() const { return false; }
bool BLERemoteCharacteristic::canWrite() const { return false; }
bool BLERemoteCharacteristic::canWriteNoResponse() const { return false; }
bool BLERemoteCharacteristic::canNotify() const { return false; }
bool BLERemoteCharacteristic::canIndicate() const { return false; }
bool BLERemoteCharacteristic::canBroadcast() const { return false; }
BTStatus BLERemoteCharacteristic::subscribe(bool, NotifyCallback) { return BTStatus::NotSupported; }
BTStatus BLERemoteCharacteristic::unsubscribe() { return BTStatus::NotSupported; }
BLERemoteDescriptor BLERemoteCharacteristic::getDescriptor(const BLEUUID &) { return BLERemoteDescriptor(); }
std::vector<BLERemoteDescriptor> BLERemoteCharacteristic::getDescriptors() const { return {}; }
BLERemoteService BLERemoteCharacteristic::getRemoteService() const { return BLERemoteService(); }
BLEUUID BLERemoteCharacteristic::getUUID() const { return BLEUUID(); }
uint16_t BLERemoteCharacteristic::getHandle() const { return 0; }
String BLERemoteCharacteristic::toString() const { return "BLERemoteCharacteristic(Bluedroid)"; }

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
