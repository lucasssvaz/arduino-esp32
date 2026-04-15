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
#include "BLEConnInfo.h"
#include <memory>
#include <functional>

class BLEService;
class BLEDescriptor;

/**
 * @brief GATT Characteristic handle.
 *
 * Lightweight shared handle wrapping a BLECharacteristic::Impl.
 * Create via service.createCharacteristic(uuid, properties).
 *
 */
class BLECharacteristic {
public:
  BLECharacteristic();
  ~BLECharacteristic() = default;
  BLECharacteristic(const BLECharacteristic &) = default;
  BLECharacteristic &operator=(const BLECharacteristic &) = default;
  BLECharacteristic(BLECharacteristic &&) = default;
  BLECharacteristic &operator=(BLECharacteristic &&) = default;

  explicit operator bool() const;

  enum NotifyStatus {
    SUCCESS_INDICATE,
    SUCCESS_NOTIFY,
    ERROR_INDICATE_DISABLED,
    ERROR_NOTIFY_DISABLED,
    ERROR_GATT,
    ERROR_NO_CLIENT,
    ERROR_NO_SUBSCRIBER,
    ERROR_INDICATE_TIMEOUT,
    ERROR_INDICATE_FAILURE,
  };

  // Handler types
  using ReadHandler = std::function<void(BLECharacteristic chr, const BLEConnInfo &conn)>;
  using WriteHandler = std::function<void(BLECharacteristic chr, const BLEConnInfo &conn)>;
  using NotifyHandler = std::function<void(BLECharacteristic chr)>;
  using SubscribeHandler = std::function<void(BLECharacteristic chr, const BLEConnInfo &conn, uint16_t subValue)>;
  using StatusHandler = std::function<void(BLECharacteristic chr, NotifyStatus status, uint32_t code)>;

  BTStatus onRead(ReadHandler handler);
  BTStatus onWrite(WriteHandler handler);
  BTStatus onNotify(NotifyHandler handler);
  BTStatus onSubscribe(SubscribeHandler handler);
  BTStatus onStatus(StatusHandler handler);

  // Value access
  void setValue(const uint8_t *data, size_t length);
  void setValue(const String &value);
  void setValue(uint16_t value);
  void setValue(uint32_t value);
  void setValue(int value);
  void setValue(float value);
  void setValue(double value);

  template<typename T>
  void setValue(const T &value) {
    setValue(reinterpret_cast<const uint8_t *>(&value), sizeof(T));
  }

  const uint8_t *getValue(size_t *length = nullptr) const;
  String getStringValue() const;

  template<typename T>
  T getValue() const {
    size_t len = 0;
    const uint8_t *data = getValue(&len);
    T result{};
    if (data && len >= sizeof(T)) {
      memcpy(&result, data, sizeof(T));
    }
    return result;
  }

  // Notifications / Indications
  BTStatus notify(const uint8_t *data = nullptr, size_t length = 0);
  BTStatus notify(uint16_t connHandle, const uint8_t *data = nullptr, size_t length = 0);
  BTStatus indicate(const uint8_t *data = nullptr, size_t length = 0);
  BTStatus indicate(uint16_t connHandle, const uint8_t *data = nullptr, size_t length = 0);

  // Properties and permissions
  BLEProperty getProperties() const;
  void setPermissions(BLEPermission permissions);
  BLEPermission getPermissions() const;

  // Descriptor management
  BLEDescriptor createDescriptor(const BLEUUID &uuid, BLEPermission perms = BLEPermission::Read, size_t maxLen = 100);
  BLEDescriptor getDescriptor(const BLEUUID &uuid);
  std::vector<BLEDescriptor> getDescriptors() const;
  void removeDescriptor(const BLEDescriptor &desc);

  // Subscription state
  size_t getSubscribedCount() const;
  std::vector<uint16_t> getSubscribedConnections() const;
  bool isSubscribed(uint16_t connHandle) const;

  BLEUUID getUUID() const;
  uint16_t getHandle() const;
  BLEService getService() const;

  void setDescription(const String &desc);
  String toString() const;

  struct Impl;

private:
  explicit BLECharacteristic(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLEServer;
  friend class BLEService;
  friend class BLEDescriptor;
};

#endif /* BLE_ENABLED */
