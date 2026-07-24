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

/**
 * @file BLEAudioCall.cpp
 * @brief Backend-agnostic CCP server/controller role handles (GTBS).
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioCall.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioCallVendor.h"
#include "esp32-hal-log.h"

// --------------------------------------------------------------------------
// Role Impls
// --------------------------------------------------------------------------

struct BLEAudioCallServer::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  std::string providerName = "ESP Phone";
  std::string uci = "un000";
  OriginateCallback onOriginate;
  TerminateCallback onTerminate;
};

struct BLEAudioCallController::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  DiscoverCallback onDiscover;
  ResultCallback onResult;
};

// --------------------------------------------------------------------------
// Single-link dispatch registry + C-callback trampolines.
// --------------------------------------------------------------------------

namespace {

std::weak_ptr<BLEAudioCallServer::Impl> s_server;
std::weak_ptr<BLEAudioCallController::Impl> s_ctrl;

BTStatus toStatus(int err) {
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

bool serverOriginateTramp(uint8_t callIndex, const char *uri) {
  auto s = s_server.lock();
  if (s && s->onOriginate) {
    return s->onOriginate(callIndex, uri ? std::string(uri) : std::string());
  }
  return true;
}

void serverTerminateTramp(uint8_t callIndex, uint8_t reason) {
  auto s = s_server.lock();
  if (s && s->onTerminate) {
    s->onTerminate(callIndex, reason);
  }
}

void ctrlDiscTramp(int err, uint8_t tbsCount, bool gtbsFound) {
  (void)tbsCount;
  auto c = s_ctrl.lock();
  if (c && c->onDiscover) {
    c->onDiscover(toStatus(err), gtbsFound);
  }
}

BLEAudioCallController::Operation toOp(ble_call_vendor_op_t op) {
  switch (op) {
    case BLE_CALL_VENDOR_OP_ACCEPT:    return BLEAudioCallController::Operation::Accept;
    case BLE_CALL_VENDOR_OP_TERMINATE: return BLEAudioCallController::Operation::Terminate;
    case BLE_CALL_VENDOR_OP_HOLD:      return BLEAudioCallController::Operation::Hold;
    case BLE_CALL_VENDOR_OP_RETRIEVE:  return BLEAudioCallController::Operation::Retrieve;
    case BLE_CALL_VENDOR_OP_ORIGINATE:
    default:                           return BLEAudioCallController::Operation::Originate;
  }
}

void ctrlOpTramp(ble_call_vendor_op_t op, int err, uint8_t callIndex) {
  auto c = s_ctrl.lock();
  if (c && c->onResult) {
    c->onResult(toOp(op), toStatus(err), callIndex);
  }
}

}  // namespace

// --------------------------------------------------------------------------
// BLEAudioCallServer
// --------------------------------------------------------------------------

BLEAudioCallServer::BLEAudioCallServer() : _impl(nullptr) {}

BLEAudioCallServer::operator bool() const {
  return _impl != nullptr;
}

BLEAudioCallServer &BLEAudioCallServer::setProviderName(const std::string &name) {
  if (_impl) {
    _impl->providerName = name;
  }
  return *this;
}
BLEAudioCallServer &BLEAudioCallServer::setUci(const std::string &uci) {
  if (_impl) {
    _impl->uci = uci;
  }
  return *this;
}

BTStatus BLEAudioCallServer::incomingCall(const std::string &from, uint8_t &callIndex) {
  if (!_impl) {
    return BTStatus::InvalidState;
  }
  uint8_t idx = 0;
  int err = bleCallVendorServerIncoming(from.c_str(), &idx);
  callIndex = idx;
  return toStatus(err);
}
BTStatus BLEAudioCallServer::terminate(uint8_t callIndex) {
  return _impl ? toStatus(bleCallVendorServerTerminate(callIndex)) : BTStatus::InvalidState;
}
BTStatus BLEAudioCallServer::updateProviderName(const std::string &name) {
  return _impl ? toStatus(bleCallVendorServerSetProviderName(name.c_str())) : BTStatus::InvalidState;
}
BLEAudioCallServer &BLEAudioCallServer::onOriginate(OriginateCallback cb) {
  if (_impl) {
    _impl->onOriginate = std::move(cb);
  }
  return *this;
}
BLEAudioCallServer &BLEAudioCallServer::onTerminated(TerminateCallback cb) {
  if (_impl) {
    _impl->onTerminate = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// BLEAudioCallController
// --------------------------------------------------------------------------

BLEAudioCallController::BLEAudioCallController() : _impl(nullptr) {}

BLEAudioCallController::operator bool() const {
  return _impl != nullptr;
}

BTStatus BLEAudioCallController::discover(uint16_t connHandle) {
  return _impl ? toStatus(bleCallVendorClientDiscover(connHandle)) : BTStatus::InvalidState;
}
BTStatus BLEAudioCallController::originate(uint16_t connHandle, const std::string &uri) {
  return _impl ? toStatus(bleCallVendorClientOriginate(connHandle, uri.c_str())) : BTStatus::InvalidState;
}
BTStatus BLEAudioCallController::accept(uint16_t connHandle, uint8_t callIndex) {
  return _impl ? toStatus(bleCallVendorClientAccept(connHandle, callIndex)) : BTStatus::InvalidState;
}
BTStatus BLEAudioCallController::terminate(uint16_t connHandle, uint8_t callIndex) {
  return _impl ? toStatus(bleCallVendorClientTerminate(connHandle, callIndex)) : BTStatus::InvalidState;
}
BLEAudioCallController &BLEAudioCallController::onDiscovered(DiscoverCallback cb) {
  if (_impl) {
    _impl->onDiscover = std::move(cb);
  }
  return *this;
}
BLEAudioCallController &BLEAudioCallController::onResult(ResultCallback cb) {
  if (_impl) {
    _impl->onResult = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// Factories (BLEAudio::create*)
// --------------------------------------------------------------------------

BLEAudioCallServer BLEAudio::createCallServer() {
  if (!_impl) {
    log_e("createCallServer() on null controller handle");
    return BLEAudioCallServer();
  }
  auto s = std::make_shared<BLEAudioCallServer::Impl>();
  s->audio = _impl;
  s_server = s;

  std::weak_ptr<BLEAudioCallServer::Impl> weak = s;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto st = weak.lock();
    if (!st) {
      return BTStatus::OK;
    }
    ble_call_vendor_server_cbs_t cbs = {};
    cbs.originate = serverOriginateTramp;
    cbs.terminated = serverTerminateTramp;
    bleCallVendorSetServerCbs(&cbs);
    int err = bleCallVendorServerInit(st->providerName.c_str(), st->uci.c_str());
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioCallServer(s);
}

BLEAudioCallController BLEAudio::createCallController() {
  if (!_impl) {
    log_e("createCallController() on null controller handle");
    return BLEAudioCallController();
  }
  auto c = std::make_shared<BLEAudioCallController::Impl>();
  c->audio = _impl;
  s_ctrl = c;

  std::weak_ptr<BLEAudioCallController::Impl> weak = c;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    ble_call_vendor_client_cbs_t cbs = {};
    cbs.discovered = ctrlDiscTramp;
    cbs.op_complete = ctrlOpTramp;
    bleCallVendorSetClientCbs(&cbs);
    int err = bleCallVendorClientInit();
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioCallController(c);
}

#endif /* BLE_AUDIO_SUPPORTED */
