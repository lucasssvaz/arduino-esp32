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
#include "BTAddress.h"
#include "BLEUUID.h"
#include "BLEProperty.h"
#include "BLEConnInfo.h"
#include "BLEAdvTypes.h"
#include "BLEService.h"
#include <memory>
#include <functional>

class BLEClass;
class BLECharacteristic;
class BLEDescriptor;
class BLEAdvertising;

/**
 * @brief GATT Server handle.
 *
 * Lightweight shared handle wrapping a BLEServer::Impl. Copying creates
 * shared ownership (refcount increment). Moving transfers ownership.
 *
 * Create via BLE.createServer(). Idempotent -- always returns a handle to
 * the same singleton server.
 */
class BLEServer {
public:
  class Callbacks {
  public:
    virtual ~Callbacks() = default;
    virtual void onConnect(BLEServer server, const BLEConnInfo &conn) {}
    virtual void onDisconnect(BLEServer server, const BLEConnInfo &conn, uint8_t reason) {}
    virtual void onMtuChanged(BLEServer server, const BLEConnInfo &conn, uint16_t mtu) {}
    virtual void onConnParamsUpdate(BLEServer server, const BLEConnInfo &conn) {}
    virtual void onIdentity(BLEServer server, const BLEConnInfo &conn) {}
  };

  BLEServer();
  ~BLEServer() = default;
  BLEServer(const BLEServer &) = default;
  BLEServer &operator=(const BLEServer &) = default;
  BLEServer(BLEServer &&) = default;
  BLEServer &operator=(BLEServer &&) = default;

  explicit operator bool() const;

  // Handler types
  using ConnectHandler = std::function<void(BLEServer server, const BLEConnInfo &conn)>;
  using DisconnectHandler = std::function<void(BLEServer server, const BLEConnInfo &conn, uint8_t reason)>;
  using MtuChangedHandler = std::function<void(BLEServer server, const BLEConnInfo &conn, uint16_t mtu)>;
  using ConnParamsHandler = std::function<void(BLEServer server, const BLEConnInfo &conn)>;
  using IdentityHandler = std::function<void(BLEServer server, const BLEConnInfo &conn)>;

  BLEService createService(const BLEUUID &uuid, uint32_t numHandles = 15, uint8_t instId = 0);
  BLEService getService(const BLEUUID &uuid);
  std::vector<BLEService> getServices() const;
  void removeService(const BLEService &service);

  BTStatus start();
  bool isStarted() const;

  BTStatus onConnect(ConnectHandler handler);
  BTStatus onDisconnect(DisconnectHandler handler);
  BTStatus onMtuChanged(MtuChangedHandler handler);
  BTStatus onConnParamsUpdate(ConnParamsHandler handler);
  BTStatus onIdentity(IdentityHandler handler);
  BTStatus setCallbacks(Callbacks &callbacks);
  void resetCallbacks();
  void advertiseOnDisconnect(bool enable);

  BLEAdvertising getAdvertising();
  BTStatus startAdvertising();
  BTStatus stopAdvertising();

  size_t getConnectedCount() const;
  std::vector<BLEConnInfo> getConnections() const;
  BTStatus disconnect(uint16_t connHandle, uint8_t reason = 0x13);
  BTStatus connect(const BTAddress &address);
  uint16_t getPeerMTU(uint16_t connHandle) const;

  BTStatus updateConnParams(uint16_t connHandle, const BLEConnParams &params);

  BTStatus setPhy(uint16_t connHandle, BLEPhy txPhy, BLEPhy rxPhy);
  BTStatus getPhy(uint16_t connHandle, BLEPhy &txPhy, BLEPhy &rxPhy) const;

  BTStatus setDataLen(uint16_t connHandle, uint16_t txOctets, uint16_t txTime);

  /** @brief Forward an internal GAP event to the server (used by advertising). */
  int handleGapEvent(void *event);

  struct Impl;

private:
  explicit BLEServer(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLEClass;
  friend class BLEService;
};

#endif /* BLE_ENABLED */
