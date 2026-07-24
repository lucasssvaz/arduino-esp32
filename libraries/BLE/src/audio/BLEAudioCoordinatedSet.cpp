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
 * @file BLEAudioCoordinatedSet.cpp
 * @brief Backend-agnostic CSIP set member/coordinator role handles.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioCoordinatedSet.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioCsipVendor.h"
#include "esp32-hal-log.h"

#include <array>
#include <cstring>

// --------------------------------------------------------------------------
// Role Impls
// --------------------------------------------------------------------------

struct BLEAudioCoordinatedSetMember::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  // Default demo SIRK; override with setSirk(). Unique per set in real designs.
  std::array<uint8_t, BLE_AUDIO_SIRK_SIZE> sirk = {0xB8, 0x03, 0xEA, 0xC6, 0xAF, 0xBB, 0x65, 0xA2, 0x5A, 0x41, 0xF1, 0x53, 0x05, 0x68, 0x8E, 0x83};
  uint8_t setSize = 2;
  uint8_t rank = 1;
  bool lockable = true;
  LockCallback onLock;
};

struct BLEAudioCoordinatedSetCoordinator::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  uint16_t conn = 0xFFFF;
  DiscoverCallback onDiscover;
};

// --------------------------------------------------------------------------
// Single-link dispatch registry + C-callback trampolines.
// --------------------------------------------------------------------------

namespace {

std::weak_ptr<BLEAudioCoordinatedSetMember::Impl> s_member;
std::weak_ptr<BLEAudioCoordinatedSetCoordinator::Impl> s_coord;

BTStatus toStatus(int err) {
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

void memberLockTramp(bool locked) {
  auto m = s_member.lock();
  if (m && m->onLock) {
    m->onLock(locked);
  }
}

void coordDiscTramp(int err, uint8_t setCount, uint8_t setSize, uint8_t rank) {
  auto c = s_coord.lock();
  if (c && c->onDiscover) {
    c->onDiscover(toStatus(err), setCount, setSize, rank);
  }
}

}  // namespace

// --------------------------------------------------------------------------
// BLEAudioCoordinatedSetMember
// --------------------------------------------------------------------------

BLEAudioCoordinatedSetMember::BLEAudioCoordinatedSetMember() : _impl(nullptr) {}

BLEAudioCoordinatedSetMember::operator bool() const {
  return _impl != nullptr;
}

BLEAudioCoordinatedSetMember &BLEAudioCoordinatedSetMember::setSirk(const uint8_t sirk[BLE_AUDIO_SIRK_SIZE]) {
  if (_impl && sirk) {
    std::memcpy(_impl->sirk.data(), sirk, BLE_AUDIO_SIRK_SIZE);
  }
  return *this;
}
BLEAudioCoordinatedSetMember &BLEAudioCoordinatedSetMember::setSetSize(uint8_t size) {
  if (_impl) {
    _impl->setSize = size;
  }
  return *this;
}
BLEAudioCoordinatedSetMember &BLEAudioCoordinatedSetMember::setRank(uint8_t rank) {
  if (_impl) {
    _impl->rank = rank;
  }
  return *this;
}
BLEAudioCoordinatedSetMember &BLEAudioCoordinatedSetMember::setLockable(bool lockable) {
  if (_impl) {
    _impl->lockable = lockable;
  }
  return *this;
}

BTStatus BLEAudioCoordinatedSetMember::setSizeAndRank(uint8_t size, uint8_t rank) {
  return _impl ? toStatus(bleCsipVendorMemberSetSizeAndRank(size, rank)) : BTStatus::InvalidState;
}
BTStatus BLEAudioCoordinatedSetMember::generateRsi(uint8_t rsi[BLE_AUDIO_RSI_SIZE]) {
  return _impl ? toStatus(bleCsipVendorMemberGenerateRsi(rsi)) : BTStatus::InvalidState;
}
BTStatus BLEAudioCoordinatedSetMember::lock() {
  return _impl ? toStatus(bleCsipVendorMemberLock(true, false)) : BTStatus::InvalidState;
}
BTStatus BLEAudioCoordinatedSetMember::unlock() {
  return _impl ? toStatus(bleCsipVendorMemberLock(false, false)) : BTStatus::InvalidState;
}
bool BLEAudioCoordinatedSetMember::isLocked() const {
  return _impl ? bleCsipVendorMemberIsLocked() : false;
}
BLEAudioCoordinatedSetMember &BLEAudioCoordinatedSetMember::onLockChanged(LockCallback cb) {
  if (_impl) {
    _impl->onLock = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// BLEAudioCoordinatedSetCoordinator
// --------------------------------------------------------------------------

BLEAudioCoordinatedSetCoordinator::BLEAudioCoordinatedSetCoordinator() : _impl(nullptr) {}

BLEAudioCoordinatedSetCoordinator::operator bool() const {
  return _impl != nullptr;
}

BTStatus BLEAudioCoordinatedSetCoordinator::discover(uint16_t connHandle) {
  if (!_impl) {
    return BTStatus::InvalidState;
  }
  _impl->conn = connHandle;
  return toStatus(bleCsipVendorCoordinatorDiscover(connHandle));
}
BLEAudioCoordinatedSetCoordinator &BLEAudioCoordinatedSetCoordinator::onDiscovered(DiscoverCallback cb) {
  if (_impl) {
    _impl->onDiscover = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// Factories (BLEAudio::create*)
// --------------------------------------------------------------------------

BLEAudioCoordinatedSetMember BLEAudio::createCoordinatedSetMember() {
  if (!_impl) {
    log_e("createCoordinatedSetMember() on null controller handle");
    return BLEAudioCoordinatedSetMember();
  }
  auto m = std::make_shared<BLEAudioCoordinatedSetMember::Impl>();
  m->audio = _impl;
  s_member = m;

  std::weak_ptr<BLEAudioCoordinatedSetMember::Impl> weak = m;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    ble_csip_vendor_member_cbs_t cbs = {};
    cbs.lock_changed = memberLockTramp;
    bleCsipVendorSetMemberCbs(&cbs);
    int err = bleCsipVendorMemberInit(s->sirk.data(), s->setSize, s->rank, s->lockable);
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioCoordinatedSetMember(m);
}

BLEAudioCoordinatedSetCoordinator BLEAudio::createCoordinatedSetCoordinator() {
  if (!_impl) {
    log_e("createCoordinatedSetCoordinator() on null controller handle");
    return BLEAudioCoordinatedSetCoordinator();
  }
  auto c = std::make_shared<BLEAudioCoordinatedSetCoordinator::Impl>();
  c->audio = _impl;
  s_coord = c;

  std::weak_ptr<BLEAudioCoordinatedSetCoordinator::Impl> weak = c;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    ble_csip_vendor_coord_cbs_t cbs = {};
    cbs.discovered = coordDiscTramp;
    bleCsipVendorSetCoordinatorCbs(&cbs);
    int err = bleCsipVendorCoordinatorInit();
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioCoordinatedSetCoordinator(c);
}

#endif /* BLE_AUDIO_SUPPORTED */
