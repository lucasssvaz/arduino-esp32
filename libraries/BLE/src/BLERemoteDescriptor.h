/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
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

#pragma once

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)

#include <memory>
#include "WString.h"
#include "BLETypes.h"

class BLERemoteCharacteristic;

/**
 * @brief Remote GATT descriptor discovered on a peer device.
 *
 * Shared handle. Obtained via BLERemoteCharacteristic::getDescriptor().
 */
class BLERemoteDescriptor {
public:
  BLERemoteDescriptor();
  ~BLERemoteDescriptor() = default;
  BLERemoteDescriptor(const BLERemoteDescriptor &) = default;
  BLERemoteDescriptor &operator=(const BLERemoteDescriptor &) = default;
  BLERemoteDescriptor(BLERemoteDescriptor &&) = default;
  BLERemoteDescriptor &operator=(BLERemoteDescriptor &&) = default;

  explicit operator bool() const;

  String readValue(uint32_t timeoutMs = 3000);
  uint8_t readUInt8(uint32_t timeoutMs = 3000);
  uint16_t readUInt16(uint32_t timeoutMs = 3000);
  uint32_t readUInt32(uint32_t timeoutMs = 3000);

  BTStatus writeValue(const uint8_t *data, size_t len, bool withResponse = true);
  BTStatus writeValue(const String &value, bool withResponse = true);
  BTStatus writeValue(uint8_t value, bool withResponse = true);

  BLERemoteCharacteristic getRemoteCharacteristic() const;
  BLEUUID getUUID() const;
  uint16_t getHandle() const;

  String toString() const;

  struct Impl;

private:
  explicit BLERemoteDescriptor(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLERemoteCharacteristic;
};

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
