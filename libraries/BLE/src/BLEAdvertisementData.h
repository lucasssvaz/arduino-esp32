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

#include "impl/BLEGuards.h"
#if BLE_ENABLED

#include <stdint.h>
#include <vector>
#include "WString.h"
#include "BLEUUID.h"

/**
 * @brief Builder for raw BLE advertisement data payloads.
 *
 * Constructs AD structures per Core Spec Vol 3, Part C, Section 11.
 */
class BLEAdvertisementData {
public:
  void setFlags(uint8_t flags);
  void setCompleteServices(const BLEUUID &uuid);
  void setPartialServices(const BLEUUID &uuid);
  void addServiceUUID(const BLEUUID &uuid);
  void setServiceData(const BLEUUID &uuid, const uint8_t *data, size_t len);
  void setManufacturerData(uint16_t companyId, const uint8_t *data, size_t len);
  void setName(const String &name, bool complete = true);
  void setShortName(const String &name);
  void setAppearance(uint16_t appearance);
  void setPreferredParams(uint16_t minInterval, uint16_t maxInterval);
  void setTxPower(int8_t txPower);
  void addRaw(const uint8_t *data, size_t len);
  void clear();

  const uint8_t *data() const;
  size_t length() const;

private:
  std::vector<uint8_t> _payload;
  void addField(uint8_t type, const uint8_t *data, size_t len);
};

#endif /* BLE_ENABLED */
