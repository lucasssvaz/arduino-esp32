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
#include <vector>
#include "WString.h"
#include "BLETypes.h"

class BLEClient;
class BLERemoteCharacteristic;

/**
 * @brief Remote GATT service discovered on a peer device.
 *
 * Shared handle. Obtained via BLEClient::getService() or BLEClient::getServices().
 */
class BLERemoteService {
public:
  BLERemoteService();
  ~BLERemoteService() = default;
  BLERemoteService(const BLERemoteService &) = default;
  BLERemoteService &operator=(const BLERemoteService &) = default;
  BLERemoteService(BLERemoteService &&) = default;
  BLERemoteService &operator=(BLERemoteService &&) = default;

  explicit operator bool() const;

  BLERemoteCharacteristic getCharacteristic(const BLEUUID &uuid);
  std::vector<BLERemoteCharacteristic> getCharacteristics() const;

  BLEClient getClient() const;
  BLEUUID getUUID() const;
  uint16_t getHandle() const;

  String getValue(const BLEUUID &charUUID);
  BTStatus setValue(const BLEUUID &charUUID, const String &value);

  String toString() const;

  struct Impl;

private:
  explicit BLERemoteService(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLEClient;
  friend class BLERemoteCharacteristic;
};

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
