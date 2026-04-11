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
// BLERemoteDescriptor -- Bluedroid backend
// --------------------------------------------------------------------------

BLERemoteDescriptor::BLERemoteDescriptor() : _impl(nullptr) {}
BLERemoteDescriptor::operator bool() const { return _impl != nullptr; }

String BLERemoteDescriptor::readValue(uint32_t) { return ""; }
uint8_t BLERemoteDescriptor::readUInt8(uint32_t) { return 0; }
uint16_t BLERemoteDescriptor::readUInt16(uint32_t) { return 0; }
uint32_t BLERemoteDescriptor::readUInt32(uint32_t) { return 0; }
BTStatus BLERemoteDescriptor::writeValue(const uint8_t *, size_t, bool) { return BTStatus::NotSupported; }
BTStatus BLERemoteDescriptor::writeValue(const String &, bool) { return BTStatus::NotSupported; }
BTStatus BLERemoteDescriptor::writeValue(uint8_t, bool) { return BTStatus::NotSupported; }
BLERemoteCharacteristic BLERemoteDescriptor::getRemoteCharacteristic() const { return BLERemoteCharacteristic(); }
BLEUUID BLERemoteDescriptor::getUUID() const { return BLEUUID(); }
uint16_t BLERemoteDescriptor::getHandle() const { return 0; }
String BLERemoteDescriptor::toString() const { return "BLERemoteDescriptor(Bluedroid)"; }

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
