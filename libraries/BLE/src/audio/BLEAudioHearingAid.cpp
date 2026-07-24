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
 * @file BLEAudioHearingAid.cpp
 * @brief Backend-agnostic HAS device/controller role handles.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioHearingAid.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioHearingAidVendor.h"
#include "esp32-hal-log.h"

#include <vector>

// --------------------------------------------------------------------------
// Role Impls
// --------------------------------------------------------------------------

struct BLEAudioHearingAidDevice::Impl {
  struct Preset {
    uint8_t index;
    std::string name;
    bool available;
    bool writable;
  };
  std::shared_ptr<BLEAudio::Impl> audio;
  BLEHearingAidType type = BLEHearingAidType::Monaural;
  bool presetSync = false;
  std::vector<Preset> presets;
  SelectCallback onSelect;
};

struct BLEAudioHearingAidController::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  BLEHearingAidType type = BLEHearingAidType::Monaural;
  DiscoverCallback onDiscover;
  PresetCallback onPreset;
  SwitchCallback onSwitch;
};

// --------------------------------------------------------------------------
// Single-link dispatch registry + C-callback trampolines.
// --------------------------------------------------------------------------

namespace {

std::weak_ptr<BLEAudioHearingAidDevice::Impl> s_device;
std::weak_ptr<BLEAudioHearingAidController::Impl> s_controller;

BTStatus toStatus(int err) {
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

void deviceSelectTramp(uint8_t index, bool sync) {
  auto d = s_device.lock();
  if (d && d->onSelect) {
    d->onSelect(index, sync);
  }
}

void controllerDiscTramp(int err, uint8_t type, uint8_t caps) {
  (void)caps;
  auto c = s_controller.lock();
  if (c) {
    c->type = static_cast<BLEHearingAidType>(type);
    if (c->onDiscover) {
      c->onDiscover(toStatus(err), c->type);
    }
  }
}

void controllerPresetReadTramp(int err, uint8_t index, bool available, const char *name, bool isLast) {
  auto c = s_controller.lock();
  if (c && c->onPreset && err == 0) {
    c->onPreset(index, available, name ? std::string(name) : std::string(), isLast);
  }
}

void controllerPresetSwitchTramp(int err, uint8_t index) {
  auto c = s_controller.lock();
  if (c && c->onSwitch) {
    c->onSwitch(toStatus(err), index);
  }
}

}  // namespace

// --------------------------------------------------------------------------
// BLEAudioHearingAidDevice
// --------------------------------------------------------------------------

BLEAudioHearingAidDevice::BLEAudioHearingAidDevice() : _impl(nullptr) {}

BLEAudioHearingAidDevice::operator bool() const {
  return _impl != nullptr;
}

BLEAudioHearingAidDevice &BLEAudioHearingAidDevice::setType(BLEHearingAidType type) {
  if (_impl) {
    _impl->type = type;
  }
  return *this;
}

BLEAudioHearingAidDevice &BLEAudioHearingAidDevice::setPresetSync(bool enabled) {
  if (_impl) {
    _impl->presetSync = enabled;
  }
  return *this;
}

BLEAudioHearingAidDevice &BLEAudioHearingAidDevice::addPreset(uint8_t index, const std::string &name, bool available, bool writable) {
  if (_impl) {
    _impl->presets.push_back({index, name, available, writable});
  }
  return *this;
}

BTStatus BLEAudioHearingAidDevice::setActivePreset(uint8_t index) {
  return _impl ? toStatus(bleHasVendorServerSetActive(index)) : BTStatus::InvalidState;
}

uint8_t BLEAudioHearingAidDevice::getActivePreset() const {
  return _impl ? bleHasVendorServerGetActive() : 0;
}

BLEAudioHearingAidDevice &BLEAudioHearingAidDevice::onPresetSelected(SelectCallback cb) {
  if (_impl) {
    _impl->onSelect = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// BLEAudioHearingAidController
// --------------------------------------------------------------------------

BLEAudioHearingAidController::BLEAudioHearingAidController() : _impl(nullptr) {}

BLEAudioHearingAidController::operator bool() const {
  return _impl != nullptr;
}

BTStatus BLEAudioHearingAidController::discover(uint16_t connHandle) {
  return _impl ? toStatus(bleHasVendorClientDiscover(connHandle)) : BTStatus::InvalidState;
}

BTStatus BLEAudioHearingAidController::readPresets(uint8_t startIndex, uint8_t maxCount) {
  return _impl ? toStatus(bleHasVendorClientReadPresets(startIndex, maxCount)) : BTStatus::InvalidState;
}

BTStatus BLEAudioHearingAidController::setActivePreset(uint8_t index, bool sync) {
  return _impl ? toStatus(bleHasVendorClientSetPreset(index, sync)) : BTStatus::InvalidState;
}

BTStatus BLEAudioHearingAidController::nextPreset(bool sync) {
  return _impl ? toStatus(bleHasVendorClientNextPreset(sync)) : BTStatus::InvalidState;
}

BTStatus BLEAudioHearingAidController::previousPreset(bool sync) {
  return _impl ? toStatus(bleHasVendorClientPrevPreset(sync)) : BTStatus::InvalidState;
}

BLEAudioHearingAidController &BLEAudioHearingAidController::onDiscovered(DiscoverCallback cb) {
  if (_impl) {
    _impl->onDiscover = std::move(cb);
  }
  return *this;
}

BLEAudioHearingAidController &BLEAudioHearingAidController::onPreset(PresetCallback cb) {
  if (_impl) {
    _impl->onPreset = std::move(cb);
  }
  return *this;
}

BLEAudioHearingAidController &BLEAudioHearingAidController::onPresetSwitch(SwitchCallback cb) {
  if (_impl) {
    _impl->onSwitch = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// Factories (BLEAudio::create*)
// --------------------------------------------------------------------------

BLEAudioHearingAidDevice BLEAudio::createHearingAidDevice() {
  if (!_impl) {
    log_e("createHearingAidDevice() on null controller handle");
    return BLEAudioHearingAidDevice();
  }
  auto d = std::make_shared<BLEAudioHearingAidDevice::Impl>();
  d->audio = _impl;
  s_device = d;
  bleHasVendorSetSelectCb(deviceSelectTramp);

  std::weak_ptr<BLEAudioHearingAidDevice::Impl> weak = d;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    int err = bleHasVendorServerInit(static_cast<uint8_t>(s->type), s->presetSync);
    if (err) {
      return BTStatus::Fail;
    }
    for (const auto &p : s->presets) {
      int perr = bleHasVendorServerAddPreset(p.index, p.writable, p.available, p.name.c_str());
      if (perr) {
        return BTStatus::Fail;
      }
    }
    return BTStatus::OK;
  });

  return BLEAudioHearingAidDevice(d);
}

BLEAudioHearingAidController BLEAudio::createHearingAidController() {
  if (!_impl) {
    log_e("createHearingAidController() on null controller handle");
    return BLEAudioHearingAidController();
  }
  auto c = std::make_shared<BLEAudioHearingAidController::Impl>();
  c->audio = _impl;
  s_controller = c;

  ble_has_vendor_client_cbs_t cbs = {};
  cbs.discovered = controllerDiscTramp;
  cbs.preset_read = controllerPresetReadTramp;
  cbs.preset_switch = controllerPresetSwitchTramp;
  bleHasVendorSetClientCbs(&cbs);

  std::weak_ptr<BLEAudioHearingAidController::Impl> weak = c;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    int err = bleHasVendorClientInit();
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioHearingAidController(c);
}

#endif /* BLE_AUDIO_SUPPORTED */
