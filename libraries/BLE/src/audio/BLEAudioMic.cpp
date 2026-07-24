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
 * @file BLEAudioMic.cpp
 * @brief Backend-agnostic MICP device/controller role handles.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioMic.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioMicVendor.h"
#include "esp32-hal-log.h"

// --------------------------------------------------------------------------
// Role Impls
// --------------------------------------------------------------------------

struct BLEAudioMicDevice::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  bool initialMute = false;
  MuteCallback onMute;
};

struct BLEAudioMicController::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  uint16_t conn = 0xFFFF;
  DiscoverCallback onDiscover;
  MuteCallback onMute;
};

// --------------------------------------------------------------------------
// Single-link dispatch registry + C-callback trampolines.
// --------------------------------------------------------------------------

namespace {

std::weak_ptr<BLEAudioMicDevice::Impl> s_dev;
std::weak_ptr<BLEAudioMicController::Impl> s_ctlr;

BTStatus toStatus(int err) {
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

void devMuteTramp(uint8_t mute) {
  auto d = s_dev.lock();
  if (d && d->onMute) {
    d->onMute(mute != 0);
  }
}

void ctlrDiscTramp(int err, uint8_t aics) {
  auto c = s_ctlr.lock();
  if (c && c->onDiscover) {
    c->onDiscover(toStatus(err), aics);
  }
}

void ctlrMuteTramp(int err, uint8_t mute) {
  auto c = s_ctlr.lock();
  if (c && c->onMute && err == 0) {
    c->onMute(mute != 0);
  }
}

}  // namespace

// --------------------------------------------------------------------------
// BLEAudioMicDevice
// --------------------------------------------------------------------------

BLEAudioMicDevice::BLEAudioMicDevice() : _impl(nullptr) {}

BLEAudioMicDevice::operator bool() const {
  return _impl != nullptr;
}

BLEAudioMicDevice &BLEAudioMicDevice::setInitialMute(bool muted) {
  if (_impl) {
    _impl->initialMute = muted;
  }
  return *this;
}

BTStatus BLEAudioMicDevice::mute() {
  return _impl ? toStatus(bleMicpVendorDeviceSetMute(true)) : BTStatus::InvalidState;
}
BTStatus BLEAudioMicDevice::unmute() {
  return _impl ? toStatus(bleMicpVendorDeviceSetMute(false)) : BTStatus::InvalidState;
}
BTStatus BLEAudioMicDevice::disableMute() {
  return _impl ? toStatus(bleMicpVendorDeviceMuteDisable()) : BTStatus::InvalidState;
}
bool BLEAudioMicDevice::isMuted() const {
  return _impl ? bleMicpVendorDeviceIsMuted() : false;
}
BLEAudioMicDevice &BLEAudioMicDevice::onMuteChanged(MuteCallback cb) {
  if (_impl) {
    _impl->onMute = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// BLEAudioMicController
// --------------------------------------------------------------------------

BLEAudioMicController::BLEAudioMicController() : _impl(nullptr) {}

BLEAudioMicController::operator bool() const {
  return _impl != nullptr;
}

BTStatus BLEAudioMicController::discover(uint16_t connHandle) {
  if (!_impl) {
    return BTStatus::InvalidState;
  }
  _impl->conn = connHandle;
  return toStatus(bleMicpVendorControllerDiscover(connHandle));
}
BTStatus BLEAudioMicController::mute() {
  return _impl ? toStatus(bleMicpVendorControllerSetMute(_impl->conn, true)) : BTStatus::InvalidState;
}
BTStatus BLEAudioMicController::unmute() {
  return _impl ? toStatus(bleMicpVendorControllerSetMute(_impl->conn, false)) : BTStatus::InvalidState;
}
BTStatus BLEAudioMicController::readMute() {
  return _impl ? toStatus(bleMicpVendorControllerReadMute(_impl->conn)) : BTStatus::InvalidState;
}
BLEAudioMicController &BLEAudioMicController::onDiscovered(DiscoverCallback cb) {
  if (_impl) {
    _impl->onDiscover = std::move(cb);
  }
  return *this;
}
BLEAudioMicController &BLEAudioMicController::onMuteChanged(MuteCallback cb) {
  if (_impl) {
    _impl->onMute = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// Factories (BLEAudio::create*)
// --------------------------------------------------------------------------

BLEAudioMicDevice BLEAudio::createMicDevice() {
  if (!_impl) {
    log_e("createMicDevice() on null controller handle");
    return BLEAudioMicDevice();
  }
  auto d = std::make_shared<BLEAudioMicDevice::Impl>();
  d->audio = _impl;
  s_dev = d;

  std::weak_ptr<BLEAudioMicDevice::Impl> weak = d;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    ble_micp_vendor_dev_cbs_t cbs = {};
    cbs.mute = devMuteTramp;
    bleMicpVendorSetDeviceCbs(&cbs);
    int err = bleMicpVendorDeviceInit(s->initialMute ? 1 : 0);
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioMicDevice(d);
}

BLEAudioMicController BLEAudio::createMicController() {
  if (!_impl) {
    log_e("createMicController() on null controller handle");
    return BLEAudioMicController();
  }
  auto c = std::make_shared<BLEAudioMicController::Impl>();
  c->audio = _impl;
  s_ctlr = c;

  std::weak_ptr<BLEAudioMicController::Impl> weak = c;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    ble_micp_vendor_ctlr_cbs_t cbs = {};
    cbs.discovered = ctlrDiscTramp;
    cbs.mute = ctlrMuteTramp;
    bleMicpVendorSetControllerCbs(&cbs);
    int err = bleMicpVendorControllerInit();
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioMicController(c);
}

#endif /* BLE_AUDIO_SUPPORTED */
