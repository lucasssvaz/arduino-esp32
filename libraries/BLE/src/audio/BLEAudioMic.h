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

#pragma once

/**
 * @file
 * @brief Microphone Control Profile (MICP) role handles.
 *
 * Two shared-handle roles, both minted by the `BLEAudio` controller:
 *  - `BLEAudioMicDevice` (MICP server): publishes the Microphone Control Service
 *    (with its Audio Input Control sub-service) and exposes the local mute state.
 *  - `BLEAudioMicController` (MICP client): after an ACL connection, discovers a
 *    peer device and drives its mute state.
 *
 * Backend-agnostic: no `esp_ble_audio_*` type appears here; the engine work
 * happens through the C-safe `BLEAudioMicVendor` boundary.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include <cstdint>
#include <functional>
#include "BTStatus.h"

class BLEAudio;

/**
 * @brief MICP Microphone Device (server) role handle.
 */
class BLEAudioMicDevice {
public:
  /** Called when the mute state changes (locally or by a remote controller). */
  using MuteCallback = std::function<void(bool muted)>;

  BLEAudioMicDevice();
  ~BLEAudioMicDevice() = default;
  BLEAudioMicDevice(const BLEAudioMicDevice &) = default;
  BLEAudioMicDevice &operator=(const BLEAudioMicDevice &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /** @brief Set the initial mute state (before audio.start()). Default unmuted. */
  BLEAudioMicDevice &setInitialMute(bool muted);

  /** @brief Mute the microphone. */
  BTStatus mute();
  /** @brief Unmute the microphone. */
  BTStatus unmute();
  /** @brief Disable mute control (spec "mute disabled" state). */
  BTStatus disableMute();
  /** @brief Whether the microphone is currently muted. */
  bool isMuted() const;

  /** @brief Set the mute-change callback. */
  BLEAudioMicDevice &onMuteChanged(MuteCallback cb);

  struct Impl;

private:
  explicit BLEAudioMicDevice(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

/**
 * @brief MICP Microphone Controller (client) role handle.
 */
class BLEAudioMicController {
public:
  using DiscoverCallback = std::function<void(BTStatus status, uint8_t aicsCount)>;
  using MuteCallback = std::function<void(bool muted)>;

  BLEAudioMicController();
  ~BLEAudioMicController() = default;
  BLEAudioMicController(const BLEAudioMicController &) = default;
  BLEAudioMicController &operator=(const BLEAudioMicController &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /**
   * @brief Discover the Microphone Control Service on an established ACL link.
   *
   * The `onDiscovered` callback fires when discovery completes; only then can
   * the control methods be used. The connection handle is remembered.
   *
   * @param connHandle ACL connection handle (e.g. `BLEClient::getHandle()`).
   * @return BTStatus::OK if discovery started, or an error code.
   */
  BTStatus discover(uint16_t connHandle);

  /** @brief Mute the remote microphone device. */
  BTStatus mute();
  /** @brief Unmute the remote microphone device. */
  BTStatus unmute();
  /** @brief Read the remote device's mute state (result via onMuteChanged). */
  BTStatus readMute();

  /** @brief Set the discovery-complete callback. */
  BLEAudioMicController &onDiscovered(DiscoverCallback cb);
  /** @brief Set the remote mute observation callback. */
  BLEAudioMicController &onMuteChanged(MuteCallback cb);

  struct Impl;

private:
  explicit BLEAudioMicController(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

#endif /* BLE_AUDIO_SUPPORTED */
