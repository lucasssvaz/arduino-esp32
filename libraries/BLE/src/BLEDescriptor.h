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

#include "BTStatus.h"
#include "BLEUUID.h"
#include "BLEProperty.h"
#include "BLEConnInfo.h"
#include <memory>
#include <functional>

class BLECharacteristic;

/**
 * @brief GATT Descriptor handle.
 *
 * Lightweight shared handle wrapping a BLEDescriptor::Impl.
 * Create via characteristic.createDescriptor(uuid, permissions).
 *
 * Standard descriptor factories:
 *   - CCCD (0x2902) is auto-created for Notify/Indicate characteristics
 *   - createUserDescription(text) creates 0x2901
 *   - createPresentationFormat() creates 0x2904
 *
 */
class BLEDescriptor {
public:
  BLEDescriptor();
  BLEDescriptor(const BLEUUID &uuid, uint16_t maxLength = 100);
  ~BLEDescriptor() = default;
  BLEDescriptor(const BLEDescriptor &) = default;
  BLEDescriptor &operator=(const BLEDescriptor &) = default;
  BLEDescriptor(BLEDescriptor &&) = default;
  BLEDescriptor &operator=(BLEDescriptor &&) = default;

  explicit operator bool() const;

  using ReadHandler = std::function<void(BLEDescriptor desc, const BLEConnInfo &conn)>;
  using WriteHandler = std::function<void(BLEDescriptor desc, const BLEConnInfo &conn)>;

  BTStatus onRead(ReadHandler handler);
  BTStatus onWrite(WriteHandler handler);

  void setValue(const uint8_t *data, size_t length);
  void setValue(const String &value);
  const uint8_t *getValue(size_t *length = nullptr) const;
  size_t getLength() const;
  void setPermissions(BLEPermission perms);

  BLEUUID getUUID() const;
  uint16_t getHandle() const;
  BLECharacteristic getCharacteristic() const;

  String toString() const;

  // Standard descriptor factories
  static BLEDescriptor createUserDescription(const String &description);
  static BLEDescriptor createCCCD();
  static BLEDescriptor createPresentationFormat();

  // 0x2901 User Description convenience
  void setUserDescription(const String &description);
  String getUserDescription() const;

  // 0x2902 CCCD convenience (auto-created for Notify/Indicate characteristics)
  bool getNotifications() const;
  bool getIndications() const;
  void setNotifications(bool enable);
  void setIndications(bool enable);

  // 0x2904 Presentation Format convenience
  void setFormat(uint8_t format);
  void setExponent(int8_t exponent);
  void setUnit(uint16_t unit);
  void setNamespace(uint8_t ns);
  void setFormatDescription(uint16_t description);

  // Format constants (Bluetooth Assigned Numbers, Format Types)
  static constexpr uint8_t FORMAT_BOOLEAN = 1;
  static constexpr uint8_t FORMAT_UINT8 = 4;
  static constexpr uint8_t FORMAT_UINT16 = 6;
  static constexpr uint8_t FORMAT_UINT32 = 8;
  static constexpr uint8_t FORMAT_SINT8 = 12;
  static constexpr uint8_t FORMAT_SINT16 = 14;
  static constexpr uint8_t FORMAT_SINT32 = 16;
  static constexpr uint8_t FORMAT_FLOAT32 = 20;
  static constexpr uint8_t FORMAT_FLOAT64 = 21;
  static constexpr uint8_t FORMAT_UTF8 = 25;
  static constexpr uint8_t FORMAT_UTF16 = 26;

  // Type queries
  bool isUserDescription() const;
  bool isCCCD() const;
  bool isPresentationFormat() const;

  struct Impl;

private:
  explicit BLEDescriptor(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLECharacteristic;
};

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
