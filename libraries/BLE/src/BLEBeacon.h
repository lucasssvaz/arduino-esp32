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

#pragma once

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)

#include "BLEUUID.h"
#include "BLEAdvertisementData.h"

/**
 * @brief Apple iBeacon helper.
 *
 * Builds/parses iBeacon advertisement data. The manufacturer-specific data
 * follows the Apple iBeacon format (company ID 0x004C, type 0x0215).
 */
class BLEBeacon {
public:
  BLEBeacon();

  BLEUUID getProximityUUID() const;
  void setProximityUUID(const BLEUUID &uuid);

  uint16_t getMajor() const;
  void setMajor(uint16_t major);

  uint16_t getMinor() const;
  void setMinor(uint16_t minor);

  uint16_t getManufacturerId() const;
  void setManufacturerId(uint16_t id);

  int8_t getSignalPower() const;
  void setSignalPower(int8_t power);

  BLEAdvertisementData getAdvertisementData() const;

  void setFromPayload(const uint8_t *payload, size_t len);

private:
  uint16_t _manufacturerId = 0x004C;
  BLEUUID _proximityUUID;
  uint16_t _major = 0;
  uint16_t _minor = 0;
  int8_t _signalPower = -59;
};

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
