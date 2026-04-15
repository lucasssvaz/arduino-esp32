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
#include "WString.h"
#include "BTStatus.h"
#include "BTAddress.h"
#include "BLEUUID.h"
#include "BLEAdvTypes.h"
#include "BLEConnInfo.h"
#include <memory>
#include <functional>

class BLEAdvertisedDevice;
class BLERemoteService;
class BLERemoteCharacteristic;
class BLERemoteDescriptor;

/**
 * @brief GATT client -- multi-instance, connect to remote peripherals.
 *
 * Create via BLE.createClient(). Each instance manages one connection.
 * Multiple BLEClient instances can coexist for multi-connection use cases.
 */
class BLEClient {
public:
  class Callbacks {
  public:
    virtual ~Callbacks() = default;
    virtual void onConnect(BLEClient client, const BLEConnInfo &conn) {}
    virtual void onDisconnect(BLEClient client, const BLEConnInfo &conn, uint8_t reason) {}
    virtual void onConnectFail(BLEClient client, int reason) {}
    virtual void onMtuChanged(BLEClient client, const BLEConnInfo &conn, uint16_t mtu) {}
    virtual bool onConnParamsUpdateRequest(BLEClient client, const BLEConnParams &params) {
      (void)client;
      (void)params;
      return true;
    }
    virtual void onIdentity(BLEClient client, const BLEConnInfo &conn) {}
  };

  BLEClient();
  ~BLEClient() = default;
  BLEClient(const BLEClient &) = default;
  BLEClient &operator=(const BLEClient &) = default;
  BLEClient(BLEClient &&) = default;
  BLEClient &operator=(BLEClient &&) = default;

  explicit operator bool() const;

  using ConnectHandler = std::function<void(BLEClient client, const BLEConnInfo &conn)>;
  using DisconnectHandler = std::function<void(BLEClient client, const BLEConnInfo &conn, uint8_t reason)>;
  using ConnectFailHandler = std::function<void(BLEClient client, int reason)>;
  using MtuChangedHandler = std::function<void(BLEClient client, const BLEConnInfo &conn, uint16_t mtu)>;
  using ConnParamsReqHandler = std::function<bool(BLEClient client, const BLEConnParams &params)>;
  using IdentityHandler = std::function<void(BLEClient client, const BLEConnInfo &conn)>;

  BTStatus connect(const BTAddress &address, uint32_t timeoutMs = 5000);
  BTStatus connect(const BLEAdvertisedDevice &device, uint32_t timeoutMs = 5000);
  BTStatus connect(const BTAddress &address, BLEPhy phy, uint32_t timeoutMs = 5000);
  BTStatus connect(const BLEAdvertisedDevice &device, BLEPhy phy, uint32_t timeoutMs = 5000);
  BTStatus connectAsync(const BTAddress &address, BLEPhy phy = BLEPhy::PHY_1M);
  BTStatus connectAsync(const BLEAdvertisedDevice &device, BLEPhy phy = BLEPhy::PHY_1M);
  BTStatus cancelConnect();
  BTStatus disconnect();
  bool isConnected() const;
  BTStatus secureConnection();

  BLERemoteService getService(const BLEUUID &uuid);
  std::vector<BLERemoteService> getServices() const;
  BTStatus discoverServices();

  String getValue(const BLEUUID &serviceUUID, const BLEUUID &charUUID);
  BTStatus setValue(const BLEUUID &serviceUUID, const BLEUUID &charUUID, const String &value);

  BTStatus onConnect(ConnectHandler handler);
  BTStatus onDisconnect(DisconnectHandler handler);
  BTStatus onConnectFail(ConnectFailHandler handler);
  BTStatus onMtuChanged(MtuChangedHandler handler);
  BTStatus onConnParamsUpdateRequest(ConnParamsReqHandler handler);
  BTStatus onIdentity(IdentityHandler handler);
  BTStatus setCallbacks(Callbacks &callbacks);
  void resetCallbacks();

  void setMTU(uint16_t mtu);
  uint16_t getMTU() const;
  int8_t getRSSI() const;
  BTAddress getPeerAddress() const;
  uint16_t getHandle() const;
  BLEConnInfo getConnection() const;

  BTStatus updateConnParams(const BLEConnParams &params);

  BTStatus setPhy(BLEPhy txPhy, BLEPhy rxPhy);
  BTStatus getPhy(BLEPhy &txPhy, BLEPhy &rxPhy) const;

  BTStatus setDataLen(uint16_t txOctets, uint16_t txTime);

  String toString() const;

  struct Impl;

private:
  explicit BLEClient(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;
  friend class BLEClass;
  friend class BLERemoteService;
  friend class BLERemoteCharacteristic;
  friend class BLERemoteDescriptor;
};

#endif /* BLE_ENABLED */
