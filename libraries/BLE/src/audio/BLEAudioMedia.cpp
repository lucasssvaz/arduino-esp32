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
 * @file BLEAudioMedia.cpp
 * @brief Backend-agnostic MCP media player/controller role handles.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioMedia.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioMediaVendor.h"
#include "esp32-hal-log.h"

// --------------------------------------------------------------------------
// Role Impls
// --------------------------------------------------------------------------

struct BLEAudioMediaPlayer::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
};

struct BLEAudioMediaController::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  DiscoverCallback onDiscover;
  StateCallback onState;
  CommandCallback onCommand;
};

// --------------------------------------------------------------------------
// Single-link dispatch registry + C-callback trampolines.
// --------------------------------------------------------------------------

namespace {

std::weak_ptr<BLEAudioMediaController::Impl> s_ctrl;

BTStatus toStatus(int err) {
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

void ctrlDiscTramp(int err) {
  auto c = s_ctrl.lock();
  if (c && c->onDiscover) {
    c->onDiscover(toStatus(err));
  }
}

void ctrlStateTramp(int err, uint8_t state) {
  auto c = s_ctrl.lock();
  if (c && c->onState && err == 0) {
    c->onState(static_cast<BLEAudioMediaState>(state));
  }
}

void ctrlCmdNtfTramp(int err, uint8_t opcode, uint8_t result) {
  auto c = s_ctrl.lock();
  if (c && c->onCommand) {
    // result==MEDIA_PROXY_CMD_SUCCESS(1) means the player accepted the opcode.
    BTStatus st = (err == 0 && result == 1) ? BTStatus::OK : BTStatus::Fail;
    c->onCommand(static_cast<BLEAudioMediaCommand>(opcode), st);
  }
}

// The send_cmd echo is informational; the authoritative result is cmd_ntf.
void ctrlCmdSentTramp(int err, uint8_t opcode) {
  (void)err;
  (void)opcode;
}

}  // namespace

// --------------------------------------------------------------------------
// BLEAudioMediaPlayer
// --------------------------------------------------------------------------

BLEAudioMediaPlayer::BLEAudioMediaPlayer() : _impl(nullptr) {}

BLEAudioMediaPlayer::operator bool() const {
  return _impl != nullptr;
}

// --------------------------------------------------------------------------
// BLEAudioMediaController
// --------------------------------------------------------------------------

BLEAudioMediaController::BLEAudioMediaController() : _impl(nullptr) {}

BLEAudioMediaController::operator bool() const {
  return _impl != nullptr;
}

BTStatus BLEAudioMediaController::discover(uint16_t connHandle) {
  return _impl ? toStatus(bleMediaVendorClientDiscover(connHandle, true)) : BTStatus::InvalidState;
}
BTStatus BLEAudioMediaController::readState(uint16_t connHandle) {
  return _impl ? toStatus(bleMediaVendorClientReadState(connHandle)) : BTStatus::InvalidState;
}
BTStatus BLEAudioMediaController::sendCommand(uint16_t connHandle, BLEAudioMediaCommand command) {
  return _impl ? toStatus(bleMediaVendorClientSendCommand(connHandle, static_cast<uint8_t>(command), false, 0)) : BTStatus::InvalidState;
}
BTStatus BLEAudioMediaController::play(uint16_t connHandle) {
  return sendCommand(connHandle, BLEAudioMediaCommand::Play);
}
BTStatus BLEAudioMediaController::pause(uint16_t connHandle) {
  return sendCommand(connHandle, BLEAudioMediaCommand::Pause);
}
BTStatus BLEAudioMediaController::stop(uint16_t connHandle) {
  return sendCommand(connHandle, BLEAudioMediaCommand::Stop);
}
BTStatus BLEAudioMediaController::nextTrack(uint16_t connHandle) {
  return sendCommand(connHandle, BLEAudioMediaCommand::NextTrack);
}
BTStatus BLEAudioMediaController::previousTrack(uint16_t connHandle) {
  return sendCommand(connHandle, BLEAudioMediaCommand::PreviousTrack);
}
BLEAudioMediaController &BLEAudioMediaController::onDiscovered(DiscoverCallback cb) {
  if (_impl) {
    _impl->onDiscover = std::move(cb);
  }
  return *this;
}
BLEAudioMediaController &BLEAudioMediaController::onStateChanged(StateCallback cb) {
  if (_impl) {
    _impl->onState = std::move(cb);
  }
  return *this;
}
BLEAudioMediaController &BLEAudioMediaController::onCommandResult(CommandCallback cb) {
  if (_impl) {
    _impl->onCommand = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// Factories (BLEAudio::create*)
// --------------------------------------------------------------------------

BLEAudioMediaPlayer BLEAudio::createMediaPlayer() {
  if (!_impl) {
    log_e("createMediaPlayer() on null controller handle");
    return BLEAudioMediaPlayer();
  }
  auto p = std::make_shared<BLEAudioMediaPlayer::Impl>();
  p->audio = _impl;

  _impl->roleApplies.push_back([]() -> BTStatus {
    int err = bleMediaVendorServerInit();
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioMediaPlayer(p);
}

BLEAudioMediaController BLEAudio::createMediaController() {
  if (!_impl) {
    log_e("createMediaController() on null controller handle");
    return BLEAudioMediaController();
  }
  auto c = std::make_shared<BLEAudioMediaController::Impl>();
  c->audio = _impl;
  s_ctrl = c;

  std::weak_ptr<BLEAudioMediaController::Impl> weak = c;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    ble_media_vendor_client_cbs_t cbs = {};
    cbs.discovered = ctrlDiscTramp;
    cbs.state = ctrlStateTramp;
    cbs.cmd_sent = ctrlCmdSentTramp;
    cbs.cmd_ntf = ctrlCmdNtfTramp;
    bleMediaVendorSetClientCbs(&cbs);
    int err = bleMediaVendorClientInit();
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioMediaController(c);
}

#endif /* BLE_AUDIO_SUPPORTED */
