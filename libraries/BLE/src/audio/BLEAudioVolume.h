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
 * @brief Volume Control Profile (VCP) role handles.
 *
 * Two shared-handle roles, both minted by the `BLEAudio` controller:
 *  - `BLEAudioVolumeRenderer` (VCP server): publishes the Volume Control Service
 *    (with the compiled-in Volume Offset / Audio Input Control sub-services) and
 *    exposes the local volume/mute, changeable locally or by a remote controller.
 *    Configure it before `audio.start()` so VCS is committed into the coordinated
 *    GATT table.
 *  - `BLEAudioVolumeController` (VCP client): after an ACL connection, discovers a
 *    peer renderer and drives its volume/mute.
 *
 * Backend-agnostic: no `esp_ble_audio_*` type appears here; the engine work
 * happens through the C-safe `BLEAudioVcpVendor` boundary.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include <cstdint>
#include <functional>
#include "BTStatus.h"

class BLEAudio;

/**
 * @brief VCP Volume Renderer (server) role handle.
 */
class BLEAudioVolumeRenderer {
public:
  /** Called when the volume/mute changes (locally or by a remote controller). */
  using StateCallback = std::function<void(uint8_t volume, bool muted)>;

  BLEAudioVolumeRenderer();
  ~BLEAudioVolumeRenderer() = default;
  BLEAudioVolumeRenderer(const BLEAudioVolumeRenderer &) = default;
  BLEAudioVolumeRenderer &operator=(const BLEAudioVolumeRenderer &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  // --- Configuration (before audio.start()) ---

  /** @brief Set the initial absolute volume (0-255). Default 100. */
  BLEAudioVolumeRenderer &setInitialVolume(uint8_t volume);
  /** @brief Set the initial mute state. Default unmuted. */
  BLEAudioVolumeRenderer &setInitialMute(bool muted);
  /** @brief Set the relative volume step size (1-255). Default 1. */
  BLEAudioVolumeRenderer &setVolumeStep(uint8_t step);

  // --- Runtime control (after audio.start()) ---

  /** @brief Set the absolute volume (0-255). */
  BTStatus setVolume(uint8_t volume);
  /** @brief Mute the renderer. */
  BTStatus mute();
  /** @brief Unmute the renderer. */
  BTStatus unmute();
  /** @brief Step the volume up by the configured step. */
  BTStatus volumeUp();
  /** @brief Step the volume down by the configured step. */
  BTStatus volumeDown();

  /** @brief Latest volume (0-255). */
  uint8_t getVolume() const;
  /** @brief Whether the renderer is currently muted. */
  bool isMuted() const;

  /** @brief Set the volume/mute change callback. */
  BLEAudioVolumeRenderer &onStateChanged(StateCallback cb);

  struct Impl;

private:
  explicit BLEAudioVolumeRenderer(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

/**
 * @brief VCP Volume Controller (client) role handle.
 */
class BLEAudioVolumeController {
public:
  /** Called when discovery of a peer renderer completes. */
  using DiscoverCallback = std::function<void(BTStatus status, uint8_t vocsCount, uint8_t aicsCount)>;
  /** Called when the remote renderer's volume/mute is observed. */
  using StateCallback = std::function<void(uint8_t volume, bool muted)>;

  BLEAudioVolumeController();
  ~BLEAudioVolumeController() = default;
  BLEAudioVolumeController(const BLEAudioVolumeController &) = default;
  BLEAudioVolumeController &operator=(const BLEAudioVolumeController &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /**
   * @brief Discover the Volume Control Service on an established ACL connection.
   *
   * The `onDiscovered` callback fires when discovery completes; only then can the
   * control methods below be used. The connection handle is remembered for those
   * subsequent calls.
   *
   * @param connHandle ACL connection handle (e.g. `BLEClient::getHandle()`).
   * @return BTStatus::OK if discovery started, or an error code.
   */
  BTStatus discover(uint16_t connHandle);

  /** @brief Set the remote renderer's absolute volume (0-255). */
  BTStatus setVolume(uint8_t volume);
  /** @brief Mute the remote renderer. */
  BTStatus mute();
  /** @brief Unmute the remote renderer. */
  BTStatus unmute();
  /** @brief Step the remote volume up. */
  BTStatus volumeUp();
  /** @brief Step the remote volume down. */
  BTStatus volumeDown();
  /** @brief Read the remote renderer's volume state (result via onStateChanged). */
  BTStatus readState();

  /** @brief Set the discovery-complete callback. */
  BLEAudioVolumeController &onDiscovered(DiscoverCallback cb);
  /** @brief Set the remote volume/mute observation callback. */
  BLEAudioVolumeController &onStateChanged(StateCallback cb);

  struct Impl;

private:
  explicit BLEAudioVolumeController(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

#endif /* BLE_AUDIO_SUPPORTED */
