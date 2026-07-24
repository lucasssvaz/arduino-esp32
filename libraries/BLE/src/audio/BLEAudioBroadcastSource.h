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
 * @brief BAP Broadcast Source (Auracast transmitter) role handle.
 *
 * A Broadcast Source streams audio one-way over a Broadcast Isochronous Group
 * (BIG), announced through extended + periodic advertising with no connection.
 * Minted by `BLEAudio::createBroadcastSource()` after `audio.begin()`; configure
 * it, call `audio.start()` (which creates the source and its BASE), then
 * `start()` to bring up the advertising carrier and begin streaming.
 *
 * Scope (Phase 2b): a single mono stream -- spec-valid and matching the
 * ESP-to-ESP Auracast demo. The `sourceStream()` handle carries transparent
 * SDUs (LC3/I2S layers on top later). Backend-agnostic: the engine work happens
 * behind the C-safe `BLEAudioBapBroadcastVendor` boundary.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include <cstdint>
#include <WString.h>
#include "BTStatus.h"
#include "audio/BLEAudioTypes.h"
#include "audio/BLEAudioStream.h"

class BLEAudio;

class BLEAudioBroadcastSource {
public:
  BLEAudioBroadcastSource();
  ~BLEAudioBroadcastSource() = default;
  BLEAudioBroadcastSource(const BLEAudioBroadcastSource &) = default;
  BLEAudioBroadcastSource &operator=(const BLEAudioBroadcastSource &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  // --- Configuration (before audio.start()) ---

  /** @brief Choose the LC3 broadcast preset (default LC3_16_2_1). */
  BLEAudioBroadcastSource &setPreset(BLEAudioCodecPreset preset);
  /** @brief Set the 24-bit Broadcast ID advertised in the announcement. */
  BLEAudioBroadcastSource &setBroadcastId(uint32_t broadcastId);
  /** @brief Set the broadcast code; a non-empty code enables BIG encryption. */
  BLEAudioBroadcastSource &setBroadcastCode(const String &code);
  /** @brief Set the BAP Broadcast Name (AD type 0x30) and PBA Program Info. */
  BLEAudioBroadcastSource &setName(const String &name);

  // --- Stream ---

  /** @brief The source stream handle (transparent SDUs sent on the BIS). */
  BLEAudioStream sourceStream() const;

  // --- Lifecycle (after audio.start()) ---

  /**
   * @brief Bring up the ext + periodic advertising carrier and start the BIG.
   *
   * Encodes the Broadcast Audio Announcement (ext adv) and BASE (periodic adv),
   * starts advertising on @p advInstance, and starts streaming. The source
   * stream becomes streaming shortly after (observe via `sourceStream()`).
   *
   * @param advInstance Extended-advertising instance to use as the BIG carrier.
   * @return BTStatus::OK on success, or an error code.
   */
  BTStatus start(uint8_t advInstance = 0);

  /** @brief Stop the BIG (streams stop; call before reconfiguring). */
  BTStatus stop();

  struct Impl;

private:
  explicit BLEAudioBroadcastSource(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

#endif /* BLE_AUDIO_SUPPORTED */
