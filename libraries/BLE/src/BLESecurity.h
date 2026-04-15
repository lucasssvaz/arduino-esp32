/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
 * Copyright 2017 chegewara
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
#include "BTAddress.h"
#include "BLEConnInfo.h"
#include <memory>

/**
 * @brief BLE Security configuration and bond management.
 *
 * Lightweight shared handle. Access via BLE.getSecurity().
 * Configures IO capability, authentication mode, passkey behavior,
 * and provides event handlers for the pairing flow.
 */
class BLESecurity {
public:
  BLESecurity();
  ~BLESecurity() = default;
  BLESecurity(const BLESecurity &) = default;
  BLESecurity &operator=(const BLESecurity &) = default;
  BLESecurity(BLESecurity &&) = default;
  BLESecurity &operator=(BLESecurity &&) = default;

  explicit operator bool() const;

  enum IOCapability : uint8_t {
    DisplayOnly = 0,
    DisplayYesNo = 1,
    KeyboardOnly = 2,
    NoInputNoOutput = 3,
    KeyboardDisplay = 4,
  };

  void setIOCapability(IOCapability cap);
  void setAuthenticationMode(bool bonding, bool mitm, bool secureConnection);

  void setPassKey(bool isStatic, uint32_t passkey = 0);
  void setStaticPassKey(uint32_t passkey);
  void setRandomPassKey();
  uint32_t getPassKey() const;
  static uint32_t generateRandomPassKey();
  void regenPassKeyOnConnect(bool enable);

  using PassKeyRequestHandler = uint32_t (*)(const BLEConnInfo &conn);
  using PassKeyDisplayHandler = void (*)(const BLEConnInfo &conn, uint32_t passKey);
  using ConfirmPassKeyHandler = bool (*)(const BLEConnInfo &conn, uint32_t passKey);
  using SecurityRequestHandler = bool (*)(const BLEConnInfo &conn);
  using AuthorizationHandler = bool (*)(const BLEConnInfo &conn, uint16_t attrHandle, bool isRead);
  using AuthCompleteHandler = void (*)(const BLEConnInfo &conn, bool success);

  BTStatus onPassKeyRequest(PassKeyRequestHandler handler);
  BTStatus onPassKeyDisplay(PassKeyDisplayHandler handler);
  BTStatus onConfirmPassKey(ConfirmPassKeyHandler handler);
  BTStatus onSecurityRequest(SecurityRequestHandler handler);
  BTStatus onAuthorization(AuthorizationHandler handler);
  BTStatus onAuthenticationComplete(AuthCompleteHandler handler);

  enum class KeyDist : uint8_t {
    EncKey = 0x01,
    IdKey = 0x02,
    SignKey = 0x04,
    LinkKey = 0x08,
  };
  friend inline constexpr KeyDist operator|(KeyDist a, KeyDist b) {
    return static_cast<KeyDist>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
  }

  void setInitiatorKeys(KeyDist keys);
  void setResponderKeys(KeyDist keys);
  void setKeySize(uint8_t size);

  void setForceAuthentication(bool force);
  bool getForceAuthentication() const;

  std::vector<BTAddress> getBondedDevices() const;
  BTStatus deleteBond(const BTAddress &address);
  BTStatus deleteAllBonds();

  using BondStoreOverflowHandler = void (*)(const BTAddress &oldestBond);
  BTStatus onBondStoreOverflow(BondStoreOverflowHandler handler);

  BTStatus startSecurity(uint16_t connHandle);
  bool waitForAuthenticationComplete(uint32_t timeoutMs = 10000);
  void resetSecurity();

  // Stack event dispatch -- called by backend GAP event handlers
  void notifyAuthComplete(const BLEConnInfo &conn, bool success);
  uint32_t resolvePasskeyForDisplay(const BLEConnInfo &conn);
  uint32_t resolvePasskeyForInput(const BLEConnInfo &conn);
  bool resolveNumericComparison(const BLEConnInfo &conn, uint32_t numcmp);
  bool notifyBondOverflow(const BTAddress &oldest);

  struct Impl;

private:
  explicit BLESecurity(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLEClass;
};

#endif /* BLE_ENABLED */
