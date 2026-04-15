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

#include <vector>
#include "BTStatus.h"
#include "BLEUUID.h"
#include "BLEProperty.h"
#include <memory>

class BLEServer;
class BLECharacteristic;

/**
 * @brief GATT Service handle.
 *
 * Lightweight shared handle wrapping a BLEService::Impl.
 * Create via server.createService(uuid).
 */
class BLEService {
public:
  BLEService();
  ~BLEService() = default;
  BLEService(const BLEService &) = default;
  BLEService &operator=(const BLEService &) = default;
  BLEService(BLEService &&) = default;
  BLEService &operator=(BLEService &&) = default;

  explicit operator bool() const;

  BLECharacteristic createCharacteristic(const BLEUUID &uuid, BLEProperty properties);
  BLECharacteristic getCharacteristic(const BLEUUID &uuid);
  std::vector<BLECharacteristic> getCharacteristics() const;
  void removeCharacteristic(const BLECharacteristic &chr);

  BTStatus start();
  void stop();
  bool isStarted() const;

  BLEUUID getUUID() const;
  uint16_t getHandle() const;
  BLEServer getServer() const;

  struct Impl;

private:
  explicit BLEService(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLEServer;
  friend class BLECharacteristic;
};

#endif /* BLE_ENABLED */
