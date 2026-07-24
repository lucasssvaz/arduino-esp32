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
 * @brief Common Audio Profile (CAP) role handles.
 *
 * CAP is the coordination layer that makes BAP streams spec-compliant. Three
 * shared-handle roles, all minted by the `BLEAudio` controller:
 *  - `BLEAudioCapAcceptor` (server): publishes CAS + an included CSIS so the
 *    device is a compliant acceptor. Pair with an `BLEAudioUnicastServer` (or
 *    broadcast sink) for the audio data plane.
 *  - `BLEAudioCapInitiator` (client): discovers CAS on a peer to confirm CAP
 *    support before coordinating unicast streams.
 *  - `BLEAudioCapCommander` (client): discovers CAS and applies coordinated
 *    volume / volume-mute / microphone-mute changes to a connected acceptor.
 *
 * Backend-agnostic: no `esp_ble_audio_*` type appears here; the engine work
 * happens through the C-safe `BLEAudioCapVendor` boundary.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include <cstdint>
#include <functional>
#include "BTStatus.h"
#include "audio/BLEAudioCoordinatedSet.h"  // BLE_AUDIO_SIRK_SIZE

class BLEAudio;

/**
 * @brief CAP Acceptor (server) role handle: CAS + included CSIS.
 */
class BLEAudioCapAcceptor {
public:
  BLEAudioCapAcceptor();
  ~BLEAudioCapAcceptor() = default;
  BLEAudioCapAcceptor(const BLEAudioCapAcceptor &) = default;
  BLEAudioCapAcceptor &operator=(const BLEAudioCapAcceptor &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  // --- Configuration (before audio.start()) ---

  /** @brief Set the 16-octet SIRK shared by all members of the set. */
  BLEAudioCapAcceptor &setSirk(const uint8_t sirk[BLE_AUDIO_SIRK_SIZE]);
  /** @brief Set the total number of members in the set. Default 2. */
  BLEAudioCapAcceptor &setSetSize(uint8_t size);
  /** @brief Set this member's rank (1..size, unique per set). Default 1. */
  BLEAudioCapAcceptor &setRank(uint8_t rank);
  /** @brief Whether the set is lockable by a commander. Default true. */
  BLEAudioCapAcceptor &setLockable(bool lockable);

  struct Impl;

private:
  explicit BLEAudioCapAcceptor(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

/**
 * @brief CAP Initiator (client) role handle.
 */
class BLEAudioCapInitiator {
public:
  /** Called when CAS discovery completes. hasCsis is true if the peer is a set. */
  using DiscoverCallback = std::function<void(BTStatus status, bool hasCsis)>;

  BLEAudioCapInitiator();
  ~BLEAudioCapInitiator() = default;
  BLEAudioCapInitiator(const BLEAudioCapInitiator &) = default;
  BLEAudioCapInitiator &operator=(const BLEAudioCapInitiator &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /**
   * @brief Discover CAS on an established ACL link to verify CAP support.
   * @param connHandle ACL connection handle.
   */
  BTStatus discover(uint16_t connHandle);

  /** @brief Set the discovery-complete callback. */
  BLEAudioCapInitiator &onDiscovered(DiscoverCallback cb);

  struct Impl;

private:
  explicit BLEAudioCapInitiator(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

/**
 * @brief CAP Commander (client) role handle.
 */
class BLEAudioCapCommander {
public:
  /** Coordinated procedure kinds reported to the result callback. */
  enum class Operation : uint8_t {
    Discover = 0,
    Volume,
    VolumeMute,
    MicMute,
  };

  /** Called when a coordinated procedure completes. */
  using ResultCallback = std::function<void(Operation op, BTStatus status)>;

  BLEAudioCapCommander();
  ~BLEAudioCapCommander() = default;
  BLEAudioCapCommander(const BLEAudioCapCommander &) = default;
  BLEAudioCapCommander &operator=(const BLEAudioCapCommander &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /** @brief Discover CAS on an established ACL link. */
  BTStatus discover(uint16_t connHandle);
  /** @brief Set absolute volume (0-255) on the connected acceptor. */
  BTStatus setVolume(uint16_t connHandle, uint8_t volume);
  /** @brief Set volume mute state on the connected acceptor. */
  BTStatus setVolumeMute(uint16_t connHandle, bool mute);
  /** @brief Set microphone mute state on the connected acceptor. */
  BTStatus setMicMute(uint16_t connHandle, bool mute);

  /** @brief Set the procedure-result callback. */
  BLEAudioCapCommander &onResult(ResultCallback cb);

  struct Impl;

private:
  explicit BLEAudioCapCommander(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

#endif /* BLE_AUDIO_SUPPORTED */
