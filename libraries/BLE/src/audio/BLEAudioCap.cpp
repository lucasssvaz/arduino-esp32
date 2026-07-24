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
 * @file BLEAudioCap.cpp
 * @brief Backend-agnostic CAP acceptor/initiator/commander role handles.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioCap.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioCapVendor.h"
#include "esp32-hal-log.h"

#include <array>
#include <cstring>

// --------------------------------------------------------------------------
// Role Impls
// --------------------------------------------------------------------------

struct BLEAudioCapAcceptor::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  std::array<uint8_t, BLE_AUDIO_SIRK_SIZE> sirk = {0xB8, 0x03, 0xEA, 0xC6, 0xAF, 0xBB, 0x65, 0xA2, 0x5A, 0x41, 0xF1, 0x53, 0x05, 0x68, 0x8E, 0x83};
  uint8_t setSize = 2;
  uint8_t rank = 1;
  bool lockable = true;
};

struct BLEAudioCapInitiator::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  DiscoverCallback onDiscover;
};

struct BLEAudioCapCommander::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  ResultCallback onResult;
};

// --------------------------------------------------------------------------
// Single-link dispatch registry + C-callback trampolines.
// --------------------------------------------------------------------------

namespace {

std::weak_ptr<BLEAudioCapInitiator::Impl> s_initiator;
std::weak_ptr<BLEAudioCapCommander::Impl> s_commander;

BTStatus toStatus(int err) {
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

void initiatorDiscTramp(int err, bool hasCsis) {
  auto i = s_initiator.lock();
  if (i && i->onDiscover) {
    i->onDiscover(toStatus(err), hasCsis);
  }
}

BLEAudioCapCommander::Operation toOp(ble_cap_vendor_op_t op) {
  switch (op) {
    case BLE_CAP_VENDOR_OP_VOLUME:      return BLEAudioCapCommander::Operation::Volume;
    case BLE_CAP_VENDOR_OP_VOLUME_MUTE: return BLEAudioCapCommander::Operation::VolumeMute;
    case BLE_CAP_VENDOR_OP_MIC_MUTE:    return BLEAudioCapCommander::Operation::MicMute;
    case BLE_CAP_VENDOR_OP_DISCOVER:
    default:                            return BLEAudioCapCommander::Operation::Discover;
  }
}

void commanderOpTramp(ble_cap_vendor_op_t op, int err) {
  auto c = s_commander.lock();
  if (c && c->onResult) {
    c->onResult(toOp(op), toStatus(err));
  }
}

}  // namespace

// --------------------------------------------------------------------------
// BLEAudioCapAcceptor
// --------------------------------------------------------------------------

BLEAudioCapAcceptor::BLEAudioCapAcceptor() : _impl(nullptr) {}

BLEAudioCapAcceptor::operator bool() const {
  return _impl != nullptr;
}

BLEAudioCapAcceptor &BLEAudioCapAcceptor::setSirk(const uint8_t sirk[BLE_AUDIO_SIRK_SIZE]) {
  if (_impl && sirk) {
    std::memcpy(_impl->sirk.data(), sirk, BLE_AUDIO_SIRK_SIZE);
  }
  return *this;
}
BLEAudioCapAcceptor &BLEAudioCapAcceptor::setSetSize(uint8_t size) {
  if (_impl) {
    _impl->setSize = size;
  }
  return *this;
}
BLEAudioCapAcceptor &BLEAudioCapAcceptor::setRank(uint8_t rank) {
  if (_impl) {
    _impl->rank = rank;
  }
  return *this;
}
BLEAudioCapAcceptor &BLEAudioCapAcceptor::setLockable(bool lockable) {
  if (_impl) {
    _impl->lockable = lockable;
  }
  return *this;
}

// --------------------------------------------------------------------------
// BLEAudioCapInitiator
// --------------------------------------------------------------------------

BLEAudioCapInitiator::BLEAudioCapInitiator() : _impl(nullptr) {}

BLEAudioCapInitiator::operator bool() const {
  return _impl != nullptr;
}

BTStatus BLEAudioCapInitiator::discover(uint16_t connHandle) {
  return _impl ? toStatus(bleCapVendorInitiatorDiscover(connHandle)) : BTStatus::InvalidState;
}
BLEAudioCapInitiator &BLEAudioCapInitiator::onDiscovered(DiscoverCallback cb) {
  if (_impl) {
    _impl->onDiscover = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// BLEAudioCapCommander
// --------------------------------------------------------------------------

BLEAudioCapCommander::BLEAudioCapCommander() : _impl(nullptr) {}

BLEAudioCapCommander::operator bool() const {
  return _impl != nullptr;
}

BTStatus BLEAudioCapCommander::discover(uint16_t connHandle) {
  return _impl ? toStatus(bleCapVendorCommanderDiscover(connHandle)) : BTStatus::InvalidState;
}
BTStatus BLEAudioCapCommander::setVolume(uint16_t connHandle, uint8_t volume) {
  return _impl ? toStatus(bleCapVendorCommanderChangeVolume(connHandle, volume)) : BTStatus::InvalidState;
}
BTStatus BLEAudioCapCommander::setVolumeMute(uint16_t connHandle, bool mute) {
  return _impl ? toStatus(bleCapVendorCommanderChangeVolumeMute(connHandle, mute)) : BTStatus::InvalidState;
}
BTStatus BLEAudioCapCommander::setMicMute(uint16_t connHandle, bool mute) {
  return _impl ? toStatus(bleCapVendorCommanderChangeMicMute(connHandle, mute)) : BTStatus::InvalidState;
}
BLEAudioCapCommander &BLEAudioCapCommander::onResult(ResultCallback cb) {
  if (_impl) {
    _impl->onResult = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// Factories (BLEAudio::create*)
// --------------------------------------------------------------------------

BLEAudioCapAcceptor BLEAudio::createCapAcceptor() {
  if (!_impl) {
    log_e("createCapAcceptor() on null controller handle");
    return BLEAudioCapAcceptor();
  }
  auto a = std::make_shared<BLEAudioCapAcceptor::Impl>();
  a->audio = _impl;

  std::weak_ptr<BLEAudioCapAcceptor::Impl> weak = a;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    int err = bleCapVendorAcceptorInit(s->sirk.data(), s->setSize, s->rank, s->lockable);
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioCapAcceptor(a);
}

BLEAudioCapInitiator BLEAudio::createCapInitiator() {
  if (!_impl) {
    log_e("createCapInitiator() on null controller handle");
    return BLEAudioCapInitiator();
  }
  auto i = std::make_shared<BLEAudioCapInitiator::Impl>();
  i->audio = _impl;
  s_initiator = i;

  std::weak_ptr<BLEAudioCapInitiator::Impl> weak = i;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    ble_cap_vendor_initiator_cbs_t cbs = {};
    cbs.discovered = initiatorDiscTramp;
    bleCapVendorSetInitiatorCbs(&cbs);
    int err = bleCapVendorInitiatorInit();
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioCapInitiator(i);
}

BLEAudioCapCommander BLEAudio::createCapCommander() {
  if (!_impl) {
    log_e("createCapCommander() on null controller handle");
    return BLEAudioCapCommander();
  }
  auto c = std::make_shared<BLEAudioCapCommander::Impl>();
  c->audio = _impl;
  s_commander = c;

  std::weak_ptr<BLEAudioCapCommander::Impl> weak = c;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    ble_cap_vendor_commander_cbs_t cbs = {};
    cbs.op_complete = commanderOpTramp;
    bleCapVendorSetCommanderCbs(&cbs);
    int err = bleCapVendorCommanderInit();
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioCapCommander(c);
}

#endif /* BLE_AUDIO_SUPPORTED */
