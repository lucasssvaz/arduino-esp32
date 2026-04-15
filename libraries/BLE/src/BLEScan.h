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

#include "BTStatus.h"
#include "BTAddress.h"
#include "BLEAdvTypes.h"
#include "BLEAdvertisedDevice.h"
#include <memory>
#include <functional>

/**
 * @brief BLE scanner -- legacy and BLE5 extended/periodic scanning.
 *
 * Singleton shared handle. Access via BLE.getScan().
 */
class BLEScan {
public:
  class Callbacks {
  public:
    virtual ~Callbacks() = default;
    virtual void onResult(BLEAdvertisedDevice device) {}
    virtual void onComplete(BLEScanResults &results) {}
    virtual void onPeriodicSync(uint16_t syncHandle, uint8_t sid, const BTAddress &addr, BLEPhy phy, uint16_t interval) {
      (void)syncHandle;
      (void)sid;
      (void)addr;
      (void)phy;
      (void)interval;
    }
    virtual void onPeriodicReport(uint16_t syncHandle, int8_t rssi, int8_t txPower, const uint8_t *data, size_t len) {
      (void)syncHandle;
      (void)rssi;
      (void)txPower;
      (void)data;
      (void)len;
    }
    virtual void onPeriodicLost(uint16_t syncHandle) { (void)syncHandle; }
  };

  BLEScan();
  ~BLEScan() = default;
  BLEScan(const BLEScan &) = default;
  BLEScan &operator=(const BLEScan &) = default;
  BLEScan(BLEScan &&) = default;
  BLEScan &operator=(BLEScan &&) = default;

  explicit operator bool() const;

  void setInterval(uint16_t intervalMs);
  void setWindow(uint16_t windowMs);
  void setActiveScan(bool active);
  void setFilterDuplicates(bool filter);
  void clearDuplicateCache();

  BTStatus start(uint32_t durationMs, bool continueExisting = false);
  BLEScanResults startBlocking(uint32_t durationMs);
  BTStatus stop();
  bool isScanning() const;

  using ResultHandler = std::function<void(BLEAdvertisedDevice)>;
  using CompleteHandler = std::function<void(BLEScanResults &)>;

  void onResult(ResultHandler callback);
  void onComplete(CompleteHandler callback);
  void setCallbacks(Callbacks &callbacks);
  void resetCallbacks();

  BLEScanResults getResults();
  void clearResults();
  void erase(const BTAddress &address);

  struct ExtScanConfig {
    BLEPhy phy = BLEPhy::PHY_1M;
    uint16_t interval = 0;
    uint16_t window = 0;
  };
  BTStatus startExtended(uint32_t durationMs, const ExtScanConfig *codedConfig = nullptr, const ExtScanConfig *uncodedConfig = nullptr);
  BTStatus stopExtended();

  BTStatus createPeriodicSync(const BTAddress &addr, uint8_t sid, uint16_t skipCount = 0, uint16_t timeoutMs = 10000);
  BTStatus cancelPeriodicSync();
  BTStatus terminatePeriodicSync(uint16_t syncHandle);

  using PeriodicSyncHandler = std::function<void(uint16_t syncHandle, uint8_t sid, const BTAddress &addr, BLEPhy phy, uint16_t interval)>;
  using PeriodicReportHandler = std::function<void(uint16_t syncHandle, int8_t rssi, int8_t txPower, const uint8_t *data, size_t len)>;
  using PeriodicLostHandler = std::function<void(uint16_t syncHandle)>;

  void onPeriodicSync(PeriodicSyncHandler handler);
  void onPeriodicReport(PeriodicReportHandler handler);
  void onPeriodicLost(PeriodicLostHandler handler);

  struct Impl;

private:
  explicit BLEScan(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLEClass;
};

#endif /* BLE_ENABLED */
