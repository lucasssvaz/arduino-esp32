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
 * @brief BAP Broadcast Sink (Auracast receiver) role handle.
 *
 * A Broadcast Sink synchronizes to a Broadcast Source's periodic-advertising
 * train, decodes its BASE, and syncs the Broadcast Isochronous Group (BIG) to
 * receive audio one-way, with no ACL connection. Minted by
 * `BLEAudio::createBroadcastSink()` after `audio.begin()`; configure it, call
 * `audio.start()` (which registers PACS + the Scan Delegator / BASS), then
 * `start()` to begin scanning and auto-sync to a matching source.
 *
 * Scope (Phase 2b): a single mono stream -- spec-valid and matching the
 * ESP-to-ESP Auracast demo. Two ways to sync:
 *  - self-initiated: `start()` scans and syncs to the first source matching the
 *    configured name / broadcast id (`BIS_SYNC_NO_PREF`, no assistant needed);
 *  - assistant-driven: a remote Broadcast Assistant steers this sink over BASS.
 *
 * The `sinkStream()` handle carries transparent SDUs (LC3/I2S layers on top
 * later). Backend-agnostic: the engine work happens behind the C-safe
 * `BLEAudioBapBroadcastVendor` boundary.
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

class BLEAudioBroadcastSink {
public:
  BLEAudioBroadcastSink();
  ~BLEAudioBroadcastSink() = default;
  BLEAudioBroadcastSink(const BLEAudioBroadcastSink &) = default;
  BLEAudioBroadcastSink &operator=(const BLEAudioBroadcastSink &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  // --- Configuration (before start()) ---

  /** @brief Choose the LC3 preset the sink advertises support for. */
  BLEAudioBroadcastSink &setPreset(BLEAudioCodecPreset preset);
  /** @brief Set the broadcast code; a non-empty code enables BIG decryption. */
  BLEAudioBroadcastSink &setBroadcastCode(const String &code);
  /** @brief Match sources by advertised name (empty = match any name). */
  BLEAudioBroadcastSink &setTargetName(const String &name);
  /** @brief Match sources by 24-bit Broadcast ID (0 = match any id). */
  BLEAudioBroadcastSink &setTargetBroadcastId(uint32_t broadcastId);

  // --- Stream ---

  /** @brief The sink stream handle (transparent SDUs received on the BIS). */
  BLEAudioStream sinkStream() const;

  // --- Lifecycle (after audio.start()) ---

  /**
   * @brief Start scanning and auto-sync to a matching Broadcast Source.
   *
   * Runs an extended scan; on the first source matching the configured name /
   * broadcast id it creates the periodic-advertising sync. The engine then
   * creates the sink, decodes the BASE, and syncs the BIG. The sink stream
   * becomes streaming shortly after (observe via `sinkStream()`).
   *
   * @return BTStatus::OK on success, or an error code.
   */
  BTStatus start();

  /** @brief Stop scanning and tear down any established sync. */
  BTStatus stop();

  struct Impl;

private:
  explicit BLEAudioBroadcastSink(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

#endif /* BLE_AUDIO_SUPPORTED */
