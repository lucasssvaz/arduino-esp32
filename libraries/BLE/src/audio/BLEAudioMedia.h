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
 * @brief Media Control Profile (MCP) role handles — **GMCS only**.
 *
 * Two shared-handle roles, both minted by the `BLEAudio` controller:
 *  - `BLEAudioMediaPlayer` (server / GMCS): publishes the engine's turnkey
 *    Generic Media Control Service (reference media player).
 *  - `BLEAudioMediaController` (client / MCC): discovers a peer's GMCS/MCS and
 *    drives playback (play/pause/stop/next/prev/...), observing media state.
 *
 * Discrete MCS and OTS are not exposed in the Arduino API (OTS is IDF draft /
 * NimBLE-only; discrete MCS adapter arrives on upstream `master`). See
 * `AUDIO.md`.
 *
 * Backend-agnostic: no `esp_ble_audio_*` type appears here; the engine work
 * happens through the C-safe `BLEAudioMediaVendor` boundary.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include <cstdint>
#include <functional>
#include "BTStatus.h"

class BLEAudio;

/** Media player state, mirrors the MCS Media State characteristic. */
enum class BLEAudioMediaState : uint8_t {
  Inactive = 0,
  Playing = 1,
  Paused = 2,
  Seeking = 3,
};

/** Media control command opcodes, as defined by the MCS specification. */
enum class BLEAudioMediaCommand : uint8_t {
  Play = 0x01,
  Pause = 0x02,
  FastRewind = 0x03,
  FastForward = 0x04,
  Stop = 0x05,
  PreviousTrack = 0x30,
  NextTrack = 0x31,
  FirstTrack = 0x32,
  LastTrack = 0x33,
  PreviousGroup = 0x40,
  NextGroup = 0x41,
};

/**
 * @brief MCP Media Player (server) role handle: turnkey MCS.
 */
class BLEAudioMediaPlayer {
public:
  BLEAudioMediaPlayer();
  ~BLEAudioMediaPlayer() = default;
  BLEAudioMediaPlayer(const BLEAudioMediaPlayer &) = default;
  BLEAudioMediaPlayer &operator=(const BLEAudioMediaPlayer &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  struct Impl;

private:
  explicit BLEAudioMediaPlayer(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

/**
 * @brief MCP Media Controller (client) role handle.
 */
class BLEAudioMediaController {
public:
  /** Called when MCS discovery completes. */
  using DiscoverCallback = std::function<void(BTStatus status)>;
  /** Called when the media state is read or notified. */
  using StateCallback = std::function<void(BLEAudioMediaState state)>;
  /** Called when a command result notification arrives. */
  using CommandCallback = std::function<void(BLEAudioMediaCommand command, BTStatus status)>;

  BLEAudioMediaController();
  ~BLEAudioMediaController() = default;
  BLEAudioMediaController(const BLEAudioMediaController &) = default;
  BLEAudioMediaController &operator=(const BLEAudioMediaController &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /**
   * @brief Discover MCS on an established ACL link and subscribe to notifications.
   * @param connHandle ACL connection handle.
   */
  BTStatus discover(uint16_t connHandle);

  /** @brief Read the peer's media state (delivered via onStateChanged). */
  BTStatus readState(uint16_t connHandle);

  /** @brief Send a media control command to the peer. */
  BTStatus sendCommand(uint16_t connHandle, BLEAudioMediaCommand command);
  /** @brief Convenience: send Play. */
  BTStatus play(uint16_t connHandle);
  /** @brief Convenience: send Pause. */
  BTStatus pause(uint16_t connHandle);
  /** @brief Convenience: send Stop. */
  BTStatus stop(uint16_t connHandle);
  /** @brief Convenience: send Next Track. */
  BTStatus nextTrack(uint16_t connHandle);
  /** @brief Convenience: send Previous Track. */
  BTStatus previousTrack(uint16_t connHandle);

  /** @brief Set the discovery-complete callback. */
  BLEAudioMediaController &onDiscovered(DiscoverCallback cb);
  /** @brief Set the media-state callback. */
  BLEAudioMediaController &onStateChanged(StateCallback cb);
  /** @brief Set the command-result callback. */
  BLEAudioMediaController &onCommandResult(CommandCallback cb);

  struct Impl;

private:
  explicit BLEAudioMediaController(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

#endif /* BLE_AUDIO_SUPPORTED */
