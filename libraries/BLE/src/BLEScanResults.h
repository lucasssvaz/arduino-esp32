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

#include <vector>
#include "BLEAdvertisedDevice.h"

class BLEScan;

/**
 * @brief Container for BLE scan results. Supports range-based for loops.
 */
class BLEScanResults {
public:
  size_t getCount() const;
  BLEAdvertisedDevice getDevice(size_t index) const;
  void dump() const;
  void appendOrReplace(const BLEAdvertisedDevice &device);

  const BLEAdvertisedDevice *begin() const;
  const BLEAdvertisedDevice *end() const;

private:
  std::vector<BLEAdvertisedDevice> _devices;
  friend class BLEScan;
};

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
