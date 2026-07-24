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
 * @brief Coordinated Set Identification Profile (CSIP) role handles.
 *
 * Two shared-handle roles, both minted by the `BLEAudio` controller:
 *  - `BLEAudioCoordinatedSetMember` (CSIP server / CSIS): publishes the SIRK,
 *    set size, and rank of a coordinated set (e.g. a stereo earbud pair), and
 *    generates the RSI to advertise for set discovery.
 *  - `BLEAudioCoordinatedSetCoordinator` (CSIP client): discovers a peer member
 *    and reads its set info (size/rank).
 *
 * Backend-agnostic: no `esp_ble_audio_*` type appears here; the engine work
 * happens through the C-safe `BLEAudioCsipVendor` boundary.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include <cstdint>
#include <functional>
#include "BTStatus.h"

class BLEAudio;

/** Size of the Set Identity Resolving Key (SIRK), in octets. */
static constexpr size_t BLE_AUDIO_SIRK_SIZE = 16;
/** Size of the Resolvable Set Identifier (RSI), in octets. */
static constexpr size_t BLE_AUDIO_RSI_SIZE = 6;

/**
 * @brief CSIP Set Member (server) role handle.
 */
class BLEAudioCoordinatedSetMember {
public:
  /** Called when the set lock state changes. */
  using LockCallback = std::function<void(bool locked)>;

  BLEAudioCoordinatedSetMember();
  ~BLEAudioCoordinatedSetMember() = default;
  BLEAudioCoordinatedSetMember(const BLEAudioCoordinatedSetMember &) = default;
  BLEAudioCoordinatedSetMember &operator=(const BLEAudioCoordinatedSetMember &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  // --- Configuration (before audio.start()) ---

  /** @brief Set the 16-octet SIRK shared by all members of the set. */
  BLEAudioCoordinatedSetMember &setSirk(const uint8_t sirk[BLE_AUDIO_SIRK_SIZE]);
  /** @brief Set the total number of members in the set. Default 2. */
  BLEAudioCoordinatedSetMember &setSetSize(uint8_t size);
  /** @brief Set this member's rank (1..size, unique per set). Default 1. */
  BLEAudioCoordinatedSetMember &setRank(uint8_t rank);
  /** @brief Whether the set is lockable by coordinators. Default true. */
  BLEAudioCoordinatedSetMember &setLockable(bool lockable);

  // --- Runtime (after audio.start()) ---

  /** @brief Update the set size and rank at runtime. */
  BTStatus setSizeAndRank(uint8_t size, uint8_t rank);
  /** @brief Generate the RSI to place in the advertising data (6 octets, LE). */
  BTStatus generateRsi(uint8_t rsi[BLE_AUDIO_RSI_SIZE]);
  /** @brief Lock the local set instance. */
  BTStatus lock();
  /** @brief Release the local set instance. */
  BTStatus unlock();
  /** @brief Whether the set is currently locked. */
  bool isLocked() const;

  /** @brief Set the lock-change callback. */
  BLEAudioCoordinatedSetMember &onLockChanged(LockCallback cb);

  struct Impl;

private:
  explicit BLEAudioCoordinatedSetMember(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

/**
 * @brief CSIP Set Coordinator (client) role handle.
 */
class BLEAudioCoordinatedSetCoordinator {
public:
  /** Called when discovery of a peer member's set(s) completes. */
  using DiscoverCallback = std::function<void(BTStatus status, uint8_t setCount, uint8_t setSize, uint8_t rank)>;

  BLEAudioCoordinatedSetCoordinator();
  ~BLEAudioCoordinatedSetCoordinator() = default;
  BLEAudioCoordinatedSetCoordinator(const BLEAudioCoordinatedSetCoordinator &) = default;
  BLEAudioCoordinatedSetCoordinator &operator=(const BLEAudioCoordinatedSetCoordinator &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /**
   * @brief Discover the peer's coordinated set(s) on an established ACL link.
   *
   * The `onDiscovered` callback fires with the set size/rank when discovery
   * completes.
   *
   * @param connHandle ACL connection handle (e.g. `BLEClient::getHandle()`).
   * @return BTStatus::OK if discovery started, or an error code.
   */
  BTStatus discover(uint16_t connHandle);

  /** @brief Set the discovery-complete callback. */
  BLEAudioCoordinatedSetCoordinator &onDiscovered(DiscoverCallback cb);

  struct Impl;

private:
  explicit BLEAudioCoordinatedSetCoordinator(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

#endif /* BLE_AUDIO_SUPPORTED */
