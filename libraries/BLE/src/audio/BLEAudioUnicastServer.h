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
 * @brief BAP Unicast Server (acceptor / peripheral) role handle.
 *
 * The Unicast Server publishes PACS capabilities and ASCS audio-stream
 * endpoints, accepts a Connected Isochronous Stream from a Unicast Client, and
 * exposes a sink stream (SDUs it receives) and a source stream (SDUs it sends).
 * Minted by `BLEAudio::createUnicastServer()` after `audio.begin()`; configure
 * it and then call `audio.start()` to commit the services.
 *
 * Backend-agnostic: no `esp_ble_audio_*` type appears here; the engine work
 * happens through the C-safe `BLEAudioBapVendor` boundary at commit time.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include "BTStatus.h"
#include "audio/BLEAudioTypes.h"
#include "audio/BLEAudioStream.h"

class BLEAudio;

class BLEAudioUnicastServer {
public:
  BLEAudioUnicastServer();
  ~BLEAudioUnicastServer() = default;
  BLEAudioUnicastServer(const BLEAudioUnicastServer &) = default;
  BLEAudioUnicastServer &operator=(const BLEAudioUnicastServer &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  // --- Configuration (before audio.start()) ---

  /** @brief Publish a sink ASE (receive audio from the client). Default: on. */
  BLEAudioUnicastServer &enableSink(bool on);
  /** @brief Publish a source ASE (send audio to the client). Default: on. */
  BLEAudioUnicastServer &enableSource(bool on);
  /** @brief Set the sink available/supported context bitmask. */
  BLEAudioUnicastServer &setSinkContexts(BLEAudioContext contexts);
  /** @brief Set the source available/supported context bitmask. */
  BLEAudioUnicastServer &setSourceContexts(BLEAudioContext contexts);
  /** @brief Set the sink audio location bitmask. */
  BLEAudioUnicastServer &setSinkLocation(BLEAudioLocation location);
  /** @brief Set the source audio location bitmask. */
  BLEAudioUnicastServer &setSourceLocation(BLEAudioLocation location);

  // --- Streams (valid once created; set callbacks any time before streaming) ---

  /** @brief The sink stream handle (SDUs received from the client). */
  BLEAudioStream sinkStream() const;
  /** @brief The source stream handle (SDUs sent to the client). */
  BLEAudioStream sourceStream() const;

  struct Impl;

private:
  explicit BLEAudioUnicastServer(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

#endif /* BLE_AUDIO_SUPPORTED */
