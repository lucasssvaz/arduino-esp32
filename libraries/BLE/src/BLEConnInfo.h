/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
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

#include <stdint.h>
#include "BTAddress.h"

enum class BLEPhy : uint8_t;

/**
 * @brief Stack-agnostic connection descriptor.
 *
 * Replaces all backend-specific parameter types (esp_ble_gatts_cb_param_t,
 * ble_gap_conn_desc, etc.) in callback signatures. This is a pure value type
 * with no heap allocation -- connection data is stored inline.
 *
 * Backend code creates instances via the friend struct BLEConnInfoImpl.
 */
class BLEConnInfo {
public:
  BLEConnInfo();

  /** @brief Connection handle assigned by the controller. */
  uint16_t getHandle() const;

  /** @brief Over-the-air (OTA) peer address. */
  BTAddress getAddress() const;

  /** @brief Identity address (after IRK resolution). Same as OTA if no IRK. */
  BTAddress getIdAddress() const;

  /** @brief Negotiated ATT MTU for this connection. */
  uint16_t getMTU() const;

  /** @brief True if the link is encrypted. */
  bool isEncrypted() const;

  /** @brief True if authenticated pairing was used. */
  bool isAuthenticated() const;

  /** @brief True if a bond (LTK) exists for this peer. */
  bool isBonded() const;

  /** @brief Encryption key size (7-16 bytes). */
  uint8_t getSecurityKeySize() const;

  /** @brief Connection interval in units of 1.25 ms. */
  uint16_t getInterval() const;

  /** @brief Peripheral latency (number of events the peripheral may skip). */
  uint16_t getLatency() const;

  /** @brief Supervision timeout in units of 10 ms. */
  uint16_t getSupervisionTimeout() const;

  /** @brief TX PHY. Returns PHY_1M on non-BLE5 chips. */
  BLEPhy getTxPhy() const;

  /** @brief RX PHY. Returns PHY_1M on non-BLE5 chips. */
  BLEPhy getRxPhy() const;

  /** @brief True if this device is the central (initiator). */
  bool isCentral() const;

  /** @brief True if this device is the peripheral (advertiser). */
  bool isPeripheral() const;

  /** @brief Last known RSSI for this connection (-127 to +20 dBm, 0 if unavailable). */
  int8_t getRSSI() const;

  /** @brief True if this object holds valid connection data. */
  explicit operator bool() const;

  static constexpr size_t kStorageSize = 56;

private:
  friend struct BLEConnInfoImpl;
  struct Data;
  alignas(8) uint8_t _storage[kStorageSize]{};
  bool _valid = false;

  Data *data();
  const Data *data() const;
};

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
