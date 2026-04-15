/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
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

/**
 * @brief Characteristic Properties -- Bluetooth Core Spec Vol 3, Part G, 3.3.1.1.
 *
 * Describes what operations a characteristic supports.
 * Combine with bitwise OR: `BLEProperty::Read | BLEProperty::Notify`.
 */
enum class BLEProperty : uint8_t {
  Broadcast = 0x01,
  Read = 0x02,
  WriteNR = 0x04,
  Write = 0x08,
  Notify = 0x10,
  Indicate = 0x20,
  SignedWrite = 0x40,
  ExtendedProps = 0x80,
};

inline constexpr BLEProperty operator|(BLEProperty a, BLEProperty b) {
  return static_cast<BLEProperty>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline constexpr BLEProperty operator|=(BLEProperty &a, BLEProperty b) {
  return a = a | b;
}

inline constexpr bool operator&(BLEProperty a, BLEProperty b) {
  return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

/**
 * @brief Attribute Access Permissions -- Bluetooth Core Spec Vol 3, Part F, 3.2.5.
 *
 * Controls security requirements for read/write access.
 * Combine with bitwise OR: `BLEPermission::ReadAuthenticated | BLEPermission::WriteAuthenticated`.
 */
enum class BLEPermission : uint16_t {
  Read = 0x0001,
  ReadEncrypted = 0x0002,
  ReadAuthenticated = 0x0004,
  ReadAuthorized = 0x0008,
  Write = 0x0010,
  WriteEncrypted = 0x0020,
  WriteAuthenticated = 0x0040,
  WriteAuthorized = 0x0080,
};

inline constexpr BLEPermission operator|(BLEPermission a, BLEPermission b) {
  return static_cast<BLEPermission>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline constexpr BLEPermission operator|=(BLEPermission &a, BLEPermission b) {
  return a = a | b;
}

inline constexpr bool operator&(BLEPermission a, BLEPermission b) {
  return (static_cast<uint16_t>(a) & static_cast<uint16_t>(b)) != 0;
}

#endif /* BLE_ENABLED */
