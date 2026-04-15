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
#include <functional>
#include "WString.h"
#include "BTStatus.h"
#include "BLEUUID.h"
#include <memory>

class BLERemoteService;
class BLERemoteDescriptor;

/**
 * @brief Remote GATT characteristic discovered on a peer device.
 *
 * Shared handle. Obtained via BLERemoteService::getCharacteristic().
 */
class BLERemoteCharacteristic {
public:
  BLERemoteCharacteristic();
  ~BLERemoteCharacteristic() = default;
  BLERemoteCharacteristic(const BLERemoteCharacteristic &) = default;
  BLERemoteCharacteristic &operator=(const BLERemoteCharacteristic &) = default;
  BLERemoteCharacteristic(BLERemoteCharacteristic &&) = default;
  BLERemoteCharacteristic &operator=(BLERemoteCharacteristic &&) = default;

  explicit operator bool() const;

  String readValue(uint32_t timeoutMs = 3000);
  uint8_t readUInt8(uint32_t timeoutMs = 3000);
  uint16_t readUInt16(uint32_t timeoutMs = 3000);
  uint32_t readUInt32(uint32_t timeoutMs = 3000);
  float readFloat(uint32_t timeoutMs = 3000);

  size_t readValue(uint8_t *buf, size_t bufLen, uint32_t timeoutMs = 3000);
  const uint8_t *readRawData(size_t *len = nullptr);

  BTStatus writeValue(const uint8_t *data, size_t len, bool withResponse = true);
  BTStatus writeValue(const String &value, bool withResponse = true);
  BTStatus writeValue(uint8_t value, bool withResponse = true);

  bool canRead() const;
  bool canWrite() const;
  bool canWriteNoResponse() const;
  bool canNotify() const;
  bool canIndicate() const;
  bool canBroadcast() const;

  using NotifyCallback = std::function<void(BLERemoteCharacteristic chr, const uint8_t *, size_t, bool)>;
  BTStatus subscribe(bool notifications = true, NotifyCallback callback = nullptr);
  BTStatus unsubscribe();

  BLERemoteDescriptor getDescriptor(const BLEUUID &uuid);
  std::vector<BLERemoteDescriptor> getDescriptors() const;
  BLERemoteService getRemoteService() const;
  BLEUUID getUUID() const;
  uint16_t getHandle() const;

  String toString() const;

  struct Impl;

private:
  explicit BLERemoteCharacteristic(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLERemoteService;
  friend class BLERemoteDescriptor;
};

#endif /* BLE_ENABLED */
