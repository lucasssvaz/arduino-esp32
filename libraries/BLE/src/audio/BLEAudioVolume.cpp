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
 * @file BLEAudioVolume.cpp
 * @brief Backend-agnostic VCP renderer/controller role handles.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioVolume.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioVcpVendor.h"
#include "esp32-hal-log.h"

// --------------------------------------------------------------------------
// Role Impls
// --------------------------------------------------------------------------

struct BLEAudioVolumeRenderer::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  uint8_t initialVolume = 100;
  bool initialMute = false;
  uint8_t step = 1;
  StateCallback onState;
};

struct BLEAudioVolumeController::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  uint16_t conn = 0xFFFF;
  DiscoverCallback onDiscover;
  StateCallback onState;
};

// --------------------------------------------------------------------------
// Single-link dispatch registry (one renderer + one controller, matching the
// vendor boundary's single-link model) and C-callback trampolines.
// --------------------------------------------------------------------------

namespace {

std::weak_ptr<BLEAudioVolumeRenderer::Impl> s_rend;
std::weak_ptr<BLEAudioVolumeController::Impl> s_ctlr;

BTStatus toStatus(int err) {
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

void rendStateTramp(uint8_t volume, uint8_t mute) {
  auto r = s_rend.lock();
  if (r && r->onState) {
    r->onState(volume, mute != 0);
  }
}

void ctlrDiscTramp(int err, uint8_t vocs, uint8_t aics) {
  auto c = s_ctlr.lock();
  if (c && c->onDiscover) {
    c->onDiscover(toStatus(err), vocs, aics);
  }
}

void ctlrStateTramp(int err, uint8_t volume, uint8_t mute) {
  auto c = s_ctlr.lock();
  if (c && c->onState && err == 0) {
    c->onState(volume, mute != 0);
  }
}

}  // namespace

// --------------------------------------------------------------------------
// BLEAudioVolumeRenderer
// --------------------------------------------------------------------------

BLEAudioVolumeRenderer::BLEAudioVolumeRenderer() : _impl(nullptr) {}

BLEAudioVolumeRenderer::operator bool() const {
  return _impl != nullptr;
}

BLEAudioVolumeRenderer &BLEAudioVolumeRenderer::setInitialVolume(uint8_t volume) {
  if (_impl) {
    _impl->initialVolume = volume;
  }
  return *this;
}
BLEAudioVolumeRenderer &BLEAudioVolumeRenderer::setInitialMute(bool muted) {
  if (_impl) {
    _impl->initialMute = muted;
  }
  return *this;
}
BLEAudioVolumeRenderer &BLEAudioVolumeRenderer::setVolumeStep(uint8_t step) {
  if (_impl) {
    _impl->step = step;
  }
  return *this;
}

BTStatus BLEAudioVolumeRenderer::setVolume(uint8_t volume) {
  return _impl ? toStatus(bleVcpVendorRendererSetVolume(volume)) : BTStatus::InvalidState;
}
BTStatus BLEAudioVolumeRenderer::mute() {
  return _impl ? toStatus(bleVcpVendorRendererSetMute(true)) : BTStatus::InvalidState;
}
BTStatus BLEAudioVolumeRenderer::unmute() {
  return _impl ? toStatus(bleVcpVendorRendererSetMute(false)) : BTStatus::InvalidState;
}
BTStatus BLEAudioVolumeRenderer::volumeUp() {
  return _impl ? toStatus(bleVcpVendorRendererVolumeUp()) : BTStatus::InvalidState;
}
BTStatus BLEAudioVolumeRenderer::volumeDown() {
  return _impl ? toStatus(bleVcpVendorRendererVolumeDown()) : BTStatus::InvalidState;
}
uint8_t BLEAudioVolumeRenderer::getVolume() const {
  return _impl ? bleVcpVendorRendererGetVolume() : 0;
}
bool BLEAudioVolumeRenderer::isMuted() const {
  return _impl ? bleVcpVendorRendererIsMuted() : false;
}
BLEAudioVolumeRenderer &BLEAudioVolumeRenderer::onStateChanged(StateCallback cb) {
  if (_impl) {
    _impl->onState = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// BLEAudioVolumeController
// --------------------------------------------------------------------------

BLEAudioVolumeController::BLEAudioVolumeController() : _impl(nullptr) {}

BLEAudioVolumeController::operator bool() const {
  return _impl != nullptr;
}

BTStatus BLEAudioVolumeController::discover(uint16_t connHandle) {
  if (!_impl) {
    return BTStatus::InvalidState;
  }
  _impl->conn = connHandle;
  return toStatus(bleVcpVendorControllerDiscover(connHandle));
}
BTStatus BLEAudioVolumeController::setVolume(uint8_t volume) {
  return _impl ? toStatus(bleVcpVendorControllerSetVolume(_impl->conn, volume)) : BTStatus::InvalidState;
}
BTStatus BLEAudioVolumeController::mute() {
  return _impl ? toStatus(bleVcpVendorControllerSetMute(_impl->conn, true)) : BTStatus::InvalidState;
}
BTStatus BLEAudioVolumeController::unmute() {
  return _impl ? toStatus(bleVcpVendorControllerSetMute(_impl->conn, false)) : BTStatus::InvalidState;
}
BTStatus BLEAudioVolumeController::volumeUp() {
  return _impl ? toStatus(bleVcpVendorControllerVolumeUp(_impl->conn)) : BTStatus::InvalidState;
}
BTStatus BLEAudioVolumeController::volumeDown() {
  return _impl ? toStatus(bleVcpVendorControllerVolumeDown(_impl->conn)) : BTStatus::InvalidState;
}
BTStatus BLEAudioVolumeController::readState() {
  return _impl ? toStatus(bleVcpVendorControllerReadState(_impl->conn)) : BTStatus::InvalidState;
}
BLEAudioVolumeController &BLEAudioVolumeController::onDiscovered(DiscoverCallback cb) {
  if (_impl) {
    _impl->onDiscover = std::move(cb);
  }
  return *this;
}
BLEAudioVolumeController &BLEAudioVolumeController::onStateChanged(StateCallback cb) {
  if (_impl) {
    _impl->onState = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// Factories (BLEAudio::create*)
// --------------------------------------------------------------------------

BLEAudioVolumeRenderer BLEAudio::createVolumeRenderer() {
  if (!_impl) {
    log_e("createVolumeRenderer() on null controller handle");
    return BLEAudioVolumeRenderer();
  }
  auto r = std::make_shared<BLEAudioVolumeRenderer::Impl>();
  r->audio = _impl;
  s_rend = r;

  std::weak_ptr<BLEAudioVolumeRenderer::Impl> weak = r;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    ble_vcp_vendor_rend_cbs_t cbs = {};
    cbs.state = rendStateTramp;
    bleVcpVendorSetRendererCbs(&cbs);
    int err = bleVcpVendorRendererInit(s->initialVolume, s->initialMute ? 1 : 0, s->step);
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioVolumeRenderer(r);
}

BLEAudioVolumeController BLEAudio::createVolumeController() {
  if (!_impl) {
    log_e("createVolumeController() on null controller handle");
    return BLEAudioVolumeController();
  }
  auto c = std::make_shared<BLEAudioVolumeController::Impl>();
  c->audio = _impl;
  s_ctlr = c;

  std::weak_ptr<BLEAudioVolumeController::Impl> weak = c;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    ble_vcp_vendor_ctlr_cbs_t cbs = {};
    cbs.discovered = ctlrDiscTramp;
    cbs.state = ctlrStateTramp;
    bleVcpVendorSetControllerCbs(&cbs);
    int err = bleVcpVendorControllerInit();
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioVolumeController(c);
}

#endif /* BLE_AUDIO_SUPPORTED */
