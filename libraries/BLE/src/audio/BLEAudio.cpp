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
 * @file BLEAudio.cpp
 * @brief Backend-agnostic bodies for the LE Audio controller handle and value types.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioEngine.h"
#include "esp32-hal-log.h"

// --------------------------------------------------------------------------
// BLEAudioCodecConfig presets (BT LE Audio spec, LC3 codec configuration)
// --------------------------------------------------------------------------

BLEAudioCodecConfig BLEAudioCodecConfig::fromPreset(BLEAudioCodecPreset preset) {
  BLEAudioCodecConfig c;
  c.framesPerSdu = 1;
  c.channelAllocation = BLEAudioLocation::Mono;

  struct Entry {
    uint32_t sr;
    uint16_t durUs;
    uint16_t octets;
  };
  // Standard BAP LC3 presets: sampling rate, frame duration, octets/frame.
  Entry e;
  switch (preset) {
    case BLEAudioCodecPreset::LC3_8_1_1:   e = {8000, 7500, 26}; break;
    case BLEAudioCodecPreset::LC3_8_2_1:   e = {8000, 10000, 30}; break;
    case BLEAudioCodecPreset::LC3_16_1_1:  e = {16000, 7500, 30}; break;
    case BLEAudioCodecPreset::LC3_16_2_1:  e = {16000, 10000, 40}; break;
    case BLEAudioCodecPreset::LC3_24_1_1:  e = {24000, 7500, 45}; break;
    case BLEAudioCodecPreset::LC3_24_2_1:  e = {24000, 10000, 60}; break;
    case BLEAudioCodecPreset::LC3_32_1_1:  e = {32000, 7500, 60}; break;
    case BLEAudioCodecPreset::LC3_32_2_1:  e = {32000, 10000, 80}; break;
    case BLEAudioCodecPreset::LC3_441_1_1: e = {44100, 7500, 97}; break;
    case BLEAudioCodecPreset::LC3_441_2_1: e = {44100, 10000, 130}; break;
    case BLEAudioCodecPreset::LC3_48_1_1:  e = {48000, 7500, 75}; break;
    case BLEAudioCodecPreset::LC3_48_2_1:  e = {48000, 10000, 100}; break;
    case BLEAudioCodecPreset::LC3_48_3_1:  e = {48000, 7500, 90}; break;
    case BLEAudioCodecPreset::LC3_48_4_1:  e = {48000, 10000, 120}; break;
    case BLEAudioCodecPreset::LC3_48_5_1:  e = {48000, 7500, 117}; break;
    case BLEAudioCodecPreset::LC3_48_6_1:  e = {48000, 10000, 155}; break;
    default:                               e = {16000, 10000, 40}; break;
  }
  c.samplingRateHz = e.sr;
  c.frameDurationUs = e.durUs;
  c.octetsPerFrame = e.octets;
  return c;
}

// --------------------------------------------------------------------------
// BLEAudio controller handle
// --------------------------------------------------------------------------

BLEAudio::BLEAudio() : _impl(nullptr) {}

BLEAudio::operator bool() const {
  return _impl != nullptr;
}

BTStatus BLEAudio::begin() {
  if (!_impl) {
    log_e("BLEAudio: begin() on null handle -- use BLE.getAudioController() after BLE.begin()");
    return BTStatus::InvalidState;
  }
  if (_impl->active) {
    return BTStatus::OK;
  }
  BTStatus st = BLEAudioEngine::init();
  if (st != BTStatus::OK) {
    return st;
  }
  _impl->active = true;
  return BTStatus::OK;
}

BTStatus BLEAudio::start() {
  if (!_impl) {
    return BTStatus::InvalidState;
  }
  if (!_impl->active) {
    log_e("BLEAudio: start() before begin()");
    return BTStatus::InvalidState;
  }
  // Apply any staged role registrations (PACS/ASCS/client) after common_init and
  // before the engine's single ble_gatts_start commit.
  for (auto &apply : _impl->roleApplies) {
    if (apply) {
      BTStatus st = apply();
      if (st != BTStatus::OK) {
        log_e("BLEAudio: role apply failed (%d)", (int)st);
        return st;
      }
    }
  }
  _impl->roleApplies.clear();
  return BLEAudioEngine::start();
}

void BLEAudio::end() {
  if (!_impl || !_impl->active) {
    return;
  }
  BLEAudioEngine::deinit();
  _impl->active = false;
}

bool BLEAudio::isActive() const {
  return _impl && _impl->active;
}

BLEAudio &BLEAudio::setPresentationDelay(uint32_t delayUs) {
  if (_impl) {
    _impl->presentationDelayUs = delayUs;
  }
  return *this;
}

uint32_t BLEAudio::getPresentationDelay() const {
  return _impl ? _impl->presentationDelayUs : 40000u;
}

#endif /* BLE_AUDIO_SUPPORTED */
