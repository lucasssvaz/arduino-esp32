/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
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
#include "BLEConnInfo.h"
#include <memory>
#include <functional>

class BLEClass;

/**
 * @brief L2CAP Connection-Oriented Channel (CoC) handle.
 *
 * Represents a single bidirectional L2CAP CoC channel. Channels are
 * created by BLEL2CAPServer (server side) or BLEClass::connectL2CAP()
 * (client side).
 *
 * L2CAP CoC channels provide a higher-throughput alternative to GATT
 * for bulk data transfer, bypassing the GATT attribute protocol overhead.
 */
class BLEL2CAPChannel {
public:
  BLEL2CAPChannel();
  ~BLEL2CAPChannel() = default;
  BLEL2CAPChannel(const BLEL2CAPChannel &) = default;
  BLEL2CAPChannel &operator=(const BLEL2CAPChannel &) = default;
  BLEL2CAPChannel(BLEL2CAPChannel &&) = default;
  BLEL2CAPChannel &operator=(BLEL2CAPChannel &&) = default;

  explicit operator bool() const;

  // Handler types
  using DataHandler = std::function<void(BLEL2CAPChannel channel, const uint8_t *data, size_t len)>;
  using DisconnectHandler = std::function<void(BLEL2CAPChannel channel)>;

  BTStatus write(const uint8_t *data, size_t len);
  BTStatus disconnect();

  BTStatus onData(DataHandler handler);
  BTStatus onDisconnect(DisconnectHandler handler);

  bool isConnected() const;
  uint16_t getPSM() const;
  uint16_t getMTU() const;
  uint16_t getConnHandle() const;

  struct Impl;

private:
  explicit BLEL2CAPChannel(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLEL2CAPServer;
  friend class BLEClass;
};

/**
 * @brief L2CAP CoC server — listens for incoming channel connections on a PSM.
 *
 * Create via BLE.createL2CAPServer(psm, mtu). When a remote device opens an
 * L2CAP CoC channel to this PSM, the onAccept callback fires with a
 * BLEL2CAPChannel handle.
 */
class BLEL2CAPServer {
public:
  BLEL2CAPServer();
  ~BLEL2CAPServer() = default;
  BLEL2CAPServer(const BLEL2CAPServer &) = default;
  BLEL2CAPServer &operator=(const BLEL2CAPServer &) = default;
  BLEL2CAPServer(BLEL2CAPServer &&) = default;
  BLEL2CAPServer &operator=(BLEL2CAPServer &&) = default;

  explicit operator bool() const;

  // Handler types
  using AcceptHandler = std::function<void(BLEL2CAPChannel channel)>;
  using DataHandler = std::function<void(BLEL2CAPChannel channel, const uint8_t *data, size_t len)>;
  using DisconnectHandler = std::function<void(BLEL2CAPChannel channel)>;

  BTStatus onAccept(AcceptHandler handler);
  BTStatus onData(DataHandler handler);
  BTStatus onDisconnect(DisconnectHandler handler);

  uint16_t getPSM() const;
  uint16_t getMTU() const;

  struct Impl;

private:
  explicit BLEL2CAPServer(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLEClass;
};

#endif /* BLE_ENABLED */
