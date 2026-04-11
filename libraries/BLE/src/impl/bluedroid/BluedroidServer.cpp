/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) && defined(CONFIG_BLUEDROID_ENABLED)

#include "BLE.h"

#include "BluedroidService.h"
#include "impl/BLESync.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <esp_gatts_api.h>
#include <esp_gap_ble_api.h>
#include <esp_bt_device.h>
#include <algorithm>
#include <map>
#include <mutex>

// --------------------------------------------------------------------------
// BLEServer::Impl -- Bluedroid backend
// --------------------------------------------------------------------------

struct BLEServer::Impl {
  bool started = false;
  bool advertiseOnDisconnect = true;
  esp_gatt_if_t gattsIf = ESP_GATT_IF_NONE;
  uint16_t appId = 0;

  std::vector<std::shared_ptr<BLEService::Impl>> services;

  BLEServer::ConnectHandler onConnectCb;
  BLEServer::DisconnectHandler onDisconnectCb;
  BLEServer::MtuChangedHandler onMtuChangedCb;
  BLEServer::ConnParamsHandler onConnParamsCb;
  BLEServer::IdentityHandler onIdentityCb;

  std::map<uint16_t, BLEConnInfo> connections;
  std::mutex mtx;
  BLESync regSync;
};

// --------------------------------------------------------------------------
// BLEServer public API -- Bluedroid
// --------------------------------------------------------------------------

BLEServer::BLEServer() : _impl(nullptr) {}
BLEServer::operator bool() const { return _impl != nullptr; }

BTStatus BLEServer::onConnect(ConnectHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  std::lock_guard<std::mutex> lock(impl.mtx);
  impl.onConnectCb = std::move(handler);
  return BTStatus::OK;
}

BTStatus BLEServer::onDisconnect(DisconnectHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  std::lock_guard<std::mutex> lock(impl.mtx);
  impl.onDisconnectCb = std::move(handler);
  return BTStatus::OK;
}

BTStatus BLEServer::onMtuChanged(MtuChangedHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  std::lock_guard<std::mutex> lock(impl.mtx);
  impl.onMtuChangedCb = std::move(handler);
  return BTStatus::OK;
}

BTStatus BLEServer::onConnParamsUpdate(ConnParamsHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  std::lock_guard<std::mutex> lock(impl.mtx);
  impl.onConnParamsCb = std::move(handler);
  return BTStatus::OK;
}

BTStatus BLEServer::onIdentity(IdentityHandler handler) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  std::lock_guard<std::mutex> lock(impl.mtx);
  impl.onIdentityCb = std::move(handler);
  return BTStatus::OK;
}

void BLEServer::advertiseOnDisconnect(bool enable) {
  BLE_CHECK_IMPL(); impl.advertiseOnDisconnect = enable;
}

BLEService BLEServer::createService(const BLEUUID &uuid, uint32_t numHandles, uint8_t instId) {
  BLE_CHECK_IMPL(BLEService());
  std::lock_guard<std::mutex> lock(impl.mtx);

  for (auto &svcImpl : impl.services) {
    if (svcImpl->uuid == uuid && svcImpl->instId == instId) {
      return BLEService(svcImpl);
    }
  }

  auto svcImpl = std::make_shared<BLEService::Impl>();
  svcImpl->uuid = uuid;
  svcImpl->numHandles = numHandles;
  svcImpl->instId = instId;
  svcImpl->serverImpl = _impl;
  impl.services.push_back(svcImpl);

  return BLEService(svcImpl);
}

BLEService BLEServer::getService(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLEService());
  std::lock_guard<std::mutex> lock(impl.mtx);
  for (auto &svcImpl : impl.services) {
    if (svcImpl->uuid == uuid) {
      return BLEService(svcImpl);
    }
  }
  return BLEService();
}

std::vector<BLEService> BLEServer::getServices() const {
  std::vector<BLEService> result;
  BLE_CHECK_IMPL(result);
  std::lock_guard<std::mutex> lock(impl.mtx);
  for (auto &svcImpl : impl.services) {
    result.push_back(BLEService(svcImpl));
  }
  return result;
}

void BLEServer::removeService(const BLEService &service) {
  if (!_impl || !service._impl) return;
  std::lock_guard<std::mutex> lock(_impl->mtx);
  auto &svcs = _impl->services;
  svcs.erase(std::remove_if(svcs.begin(), svcs.end(),
    [&](const std::shared_ptr<BLEService::Impl> &s) { return s == service._impl; }), svcs.end());
}

BTStatus BLEServer::start() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (impl.started) return BTStatus::OK;
  impl.started = true;
  return BTStatus::OK;
}

bool BLEServer::isStarted() const { return _impl && _impl->started; }

size_t BLEServer::getConnectedCount() const {
  BLE_CHECK_IMPL(0);
  std::lock_guard<std::mutex> lock(impl.mtx);
  return impl.connections.size();
}

std::vector<BLEConnInfo> BLEServer::getConnections() const {
  std::vector<BLEConnInfo> result;
  BLE_CHECK_IMPL(result);
  std::lock_guard<std::mutex> lock(impl.mtx);
  for (const auto &pair : impl.connections) result.push_back(pair.second);
  return result;
}

BTStatus BLEServer::disconnect(uint16_t connHandle, uint8_t /*reason*/) {
  if (!_impl) return BTStatus::InvalidState;
  // Bluedroid disconnect needs BDA, not conn handle -- simplified for now
  return BTStatus::NotSupported;
}

BTStatus BLEServer::connect(const BTAddress & /*address*/) { return BTStatus::NotSupported; }
uint16_t BLEServer::getPeerMTU(uint16_t /*connHandle*/) const { return 23; }

BTStatus BLEServer::updateConnParams(uint16_t /*connHandle*/, const BLEConnParams & /*params*/) {
  return BTStatus::NotSupported;
}

BTStatus BLEServer::setPhy(uint16_t /*connHandle*/, BLEPhy /*txPhy*/, BLEPhy /*rxPhy*/) {
  return BTStatus::NotSupported;
}

BTStatus BLEServer::getPhy(uint16_t /*connHandle*/, BLEPhy & /*txPhy*/, BLEPhy & /*rxPhy*/) const {
  return BTStatus::NotSupported;
}

BTStatus BLEServer::setDataLen(uint16_t /*connHandle*/, uint16_t /*txOctets*/, uint16_t /*txTime*/) {
  return BTStatus::NotSupported;
}

int BLEServer::handleGapEvent(void *event) { return 0; }

BLEAdvertising BLEServer::getAdvertising() { return BLE.getAdvertising(); }
BTStatus BLEServer::startAdvertising() { return BLE.startAdvertising(); }
BTStatus BLEServer::stopAdvertising() { return BLE.stopAdvertising(); }

// --------------------------------------------------------------------------
// BLEClass::createServer() -- Bluedroid factory
// --------------------------------------------------------------------------

BLEServer BLEClass::createServer() {
  if (!isInitialized()) return BLEServer();
  static std::shared_ptr<BLEServer::Impl> serverImpl;
  if (!serverImpl) {
    serverImpl = std::make_shared<BLEServer::Impl>();
  }
  return BLEServer(serverImpl);
}

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
