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

class BLEScan;

/**
 * @brief Represents a single advertised BLE device discovered during scanning.
 *
 * Shared handle. Obtained via BLEScan callbacks or BLEScanResults.
 * Contains parsed advertisement data (name, UUIDs, manufacturer data, etc.).
 */
class BLEAdvertisedDevice {
public:
  BLEAdvertisedDevice();
  ~BLEAdvertisedDevice() = default;
  BLEAdvertisedDevice(const BLEAdvertisedDevice &) = default;
  BLEAdvertisedDevice &operator=(const BLEAdvertisedDevice &) = default;
  BLEAdvertisedDevice(BLEAdvertisedDevice &&) = default;
  BLEAdvertisedDevice &operator=(BLEAdvertisedDevice &&) = default;

  explicit operator bool() const;

  BTAddress getAddress() const;
  BTAddress::Type getAddressType() const;
  String getName() const;
  int8_t getRSSI() const;
  int8_t getTXPower() const;
  uint16_t getAppearance() const;
  BLEAdvType getAdvType() const;

  const uint8_t *getManufacturerData(size_t *len) const;
  String getManufacturerDataString() const;
  uint16_t getManufacturerCompanyId() const;

  size_t getServiceUUIDCount() const;
  BLEUUID getServiceUUID(size_t index = 0) const;
  bool haveServiceUUID() const;
  bool isAdvertisingService(const BLEUUID &uuid) const;

  size_t getServiceDataCount() const;
  const uint8_t *getServiceData(size_t index, size_t *len) const;
  String getServiceDataString(size_t index = 0) const;
  BLEUUID getServiceDataUUID(size_t index = 0) const;
  bool haveServiceData() const;

  const uint8_t *getPayload() const;
  size_t getPayloadLength() const;

  bool haveName() const;
  bool haveRSSI() const;
  bool haveTXPower() const;
  bool haveAppearance() const;
  bool haveManufacturerData() const;
  bool isConnectable() const;
  bool isScannable() const;
  bool isDirected() const;
  bool isLegacyAdvertisement() const;

  BLEPhy getPrimaryPhy() const;
  BLEPhy getSecondaryPhy() const;
  uint8_t getAdvSID() const;
  uint16_t getPeriodicInterval() const;

  enum FrameType { Unknown, EddystoneUUID, EddystoneURL, EddystoneTLM };
  FrameType getFrameType() const;

  String toString() const;

  struct Impl;

private:
  explicit BLEAdvertisedDevice(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLEScan;
};

#include "BLEScanResults.h"

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
