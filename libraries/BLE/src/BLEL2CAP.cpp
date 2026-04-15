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

#include "impl/BLEGuards.h"
#if BLE_ENABLED && !BLE_L2CAP_SUPPORTED

#include "BLEL2CAP.h"
#include "BLE.h"

BLEL2CAPChannel::BLEL2CAPChannel() : _impl(nullptr) {}
BLEL2CAPChannel::operator bool() const { return false; }
BTStatus BLEL2CAPChannel::write(const uint8_t *, size_t) { return BTStatus::NotSupported; }
BTStatus BLEL2CAPChannel::disconnect() { return BTStatus::NotSupported; }
BTStatus BLEL2CAPChannel::onData(DataHandler) { return BTStatus::NotSupported; }
BTStatus BLEL2CAPChannel::onDisconnect(DisconnectHandler) { return BTStatus::NotSupported; }
bool BLEL2CAPChannel::isConnected() const { return false; }
uint16_t BLEL2CAPChannel::getPSM() const { return 0; }
uint16_t BLEL2CAPChannel::getMTU() const { return 0; }
uint16_t BLEL2CAPChannel::getConnHandle() const { return 0; }

BLEL2CAPServer::BLEL2CAPServer() : _impl(nullptr) {}
BLEL2CAPServer::operator bool() const { return false; }
BTStatus BLEL2CAPServer::onAccept(AcceptHandler) { return BTStatus::NotSupported; }
BTStatus BLEL2CAPServer::onData(DataHandler) { return BTStatus::NotSupported; }
BTStatus BLEL2CAPServer::onDisconnect(DisconnectHandler) { return BTStatus::NotSupported; }
uint16_t BLEL2CAPServer::getPSM() const { return 0; }
uint16_t BLEL2CAPServer::getMTU() const { return 0; }

BLEL2CAPServer BLEClass::createL2CAPServer(uint16_t, uint16_t) { return BLEL2CAPServer(); }
BLEL2CAPChannel BLEClass::connectL2CAP(uint16_t, uint16_t, uint16_t) { return BLEL2CAPChannel(); }

#endif
