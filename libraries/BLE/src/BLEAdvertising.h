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
#include <functional>
#include "BTStatus.h"
#include "BTAddress.h"
#include "BLEAdvTypes.h"
#include "BLEAdvertisementData.h"

/**
 * @brief Unified BLE advertising class covering legacy, extended, and periodic advertising.
 *
 * Legacy advertising is the simple default. BLE5 extended/periodic features
 * are available with explicit opt-in and guarded by BLE5_SUPPORTED.
 */
class BLEAdvertising {
public:
  BLEAdvertising();
  ~BLEAdvertising() = default;
  BLEAdvertising(const BLEAdvertising &) = default;
  BLEAdvertising &operator=(const BLEAdvertising &) = default;
  BLEAdvertising(BLEAdvertising &&) = default;
  BLEAdvertising &operator=(BLEAdvertising &&) = default;

  explicit operator bool() const;

  // --- Legacy Advertising ---
  void addServiceUUID(const BLEUUID &uuid);
  void removeServiceUUID(const BLEUUID &uuid);
  void clearServiceUUIDs();

  void setName(const String &name);
  void setScanResponse(bool enable);
  void setType(BLEAdvType type);
  void setInterval(uint16_t minMs, uint16_t maxMs);
  void setMinPreferred(uint16_t minPreferred);
  void setMaxPreferred(uint16_t maxPreferred);
  void setTxPower(bool include);
  void setAppearance(uint16_t appearance);
  void setScanFilter(bool scanRequestWhitelistOnly, bool connectWhitelistOnly);
  void reset();

  void setAdvertisementData(const BLEAdvertisementData &data);
  void setScanResponseData(const BLEAdvertisementData &data);

  BTStatus start(uint32_t durationMs = 0);
  BTStatus stop();
  bool isAdvertising() const;

  // --- Extended Advertising (BLE5) ---
  struct ExtAdvConfig {
    uint8_t instance = 0;
    BLEAdvType type = BLEAdvType::ConnectableScannable;
    BLEPhy primaryPhy = BLEPhy::PHY_1M;
    BLEPhy secondaryPhy = BLEPhy::PHY_1M;
    int8_t txPower = 127;
    uint16_t intervalMin = 0x30;
    uint16_t intervalMax = 0x30;
    uint8_t channelMap = 0x07;
    uint8_t sid = 0;
    bool anonymous = false;
    bool includeTxPower = false;
    bool scanReqNotify = false;
  };

  BTStatus configureExtended(const ExtAdvConfig &config);
  BTStatus setExtAdvertisementData(uint8_t instance, const BLEAdvertisementData &data);
  BTStatus setExtScanResponseData(uint8_t instance, const BLEAdvertisementData &data);
  BTStatus setExtInstanceAddress(uint8_t instance, const BTAddress &addr);
  BTStatus startExtended(uint8_t instance, uint32_t durationMs = 0, uint8_t maxEvents = 0);
  BTStatus stopExtended(uint8_t instance);
  BTStatus removeExtended(uint8_t instance);
  BTStatus clearExtended();

  // --- Periodic Advertising (BLE5) ---
  struct PeriodicAdvConfig {
    uint8_t instance = 0;
    uint16_t intervalMin = 0;
    uint16_t intervalMax = 0;
    bool includeTxPower = false;
  };

  BTStatus configurePeriodicAdv(const PeriodicAdvConfig &config);
  BTStatus setPeriodicAdvData(uint8_t instance, const BLEAdvertisementData &data);
  BTStatus startPeriodicAdv(uint8_t instance);
  BTStatus stopPeriodicAdv(uint8_t instance);

  // --- Event Handlers ---
  using CompleteHandler = std::function<void(uint8_t instance)>;
  BTStatus onComplete(CompleteHandler handler);

  struct Impl;

private:
  explicit BLEAdvertising(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLEClass;
};

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
