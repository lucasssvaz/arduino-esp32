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
 * @file BLEAudioUnicastClient.cpp
 * @brief Backend-agnostic BAP Unicast Client role handle.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioUnicastClient.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioStreamInternal.h"
#include "audio/BLEAudioBapVendor.h"
#include "esp32-hal-log.h"

struct BLEAudioUnicastClient::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  BLEAudioCodecPreset preset = BLEAudioCodecPreset::LC3_16_2_1;
  std::shared_ptr<BLEAudioStream::Impl> txStreamImpl;  // DIR_SOURCE (to peer sink)
  std::shared_ptr<BLEAudioStream::Impl> rxStreamImpl;  // DIR_SINK   (from peer source)
};

static ble_bap_vendor_preset_t mapPreset(BLEAudioCodecPreset p) {
  switch (p) {
    case BLEAudioCodecPreset::LC3_24_2_1: return BLE_BAP_VENDOR_PRESET_24_2_1;
    case BLEAudioCodecPreset::LC3_48_4_1: return BLE_BAP_VENDOR_PRESET_48_4_1;
    default:                              return BLE_BAP_VENDOR_PRESET_16_2_1;
  }
}

BLEAudioUnicastClient::BLEAudioUnicastClient() : _impl(nullptr) {}

BLEAudioUnicastClient::operator bool() const {
  return _impl != nullptr;
}

BLEAudioUnicastClient &BLEAudioUnicastClient::setPreset(BLEAudioCodecPreset preset) {
  if (_impl) {
    _impl->preset = preset;
  }
  return *this;
}

BTStatus BLEAudioUnicastClient::connect(uint16_t connHandle) {
  if (!_impl) {
    return BTStatus::InvalidState;
  }
  int err = bleBapVendorClientStart(connHandle, mapPreset(_impl->preset));
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

void BLEAudioUnicastClient::reset() {
  bleBapVendorClientReset();
}

BLEAudioStream BLEAudioUnicastClient::txStream() const {
  return _impl ? BLEAudioStream(_impl->txStreamImpl) : BLEAudioStream();
}
BLEAudioStream BLEAudioUnicastClient::rxStream() const {
  return _impl ? BLEAudioStream(_impl->rxStreamImpl) : BLEAudioStream();
}

// --------------------------------------------------------------------------
// Factory
// --------------------------------------------------------------------------

BLEAudioUnicastClient BLEAudio::createUnicastClient() {
  using namespace BLEAudioStreamInternal;
  if (!_impl) {
    log_e("createUnicastClient() on null controller handle");
    return BLEAudioUnicastClient();
  }

  auto cli = std::make_shared<BLEAudioUnicastClient::Impl>();
  cli->audio = _impl;
  cli->txStreamImpl = makeStream(DIR_SOURCE);
  cli->rxStreamImpl = makeStream(DIR_SINK);

  std::weak_ptr<BLEAudioUnicastClient::Impl> weak = cli;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto c = weak.lock();
    if (!c) {
      return BTStatus::OK;
    }
    registerForDispatch(c->txStreamImpl);
    registerForDispatch(c->rxStreamImpl);
    installVendorStreamCbs();
    int err = bleBapVendorClientInit();
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioUnicastClient(cli);
}

#endif /* BLE_AUDIO_SUPPORTED */
