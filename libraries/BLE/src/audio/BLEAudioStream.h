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
 * @brief Per-stream handle for a BAP audio stream (one ASE / ISO direction).
 *
 * A `BLEAudioStream` is a shared handle (like the rest of the library's value
 * types) to one directional audio stream: a *source* stream the local device
 * transmits on, or a *sink* stream it receives on. Role handles
 * (`BLEAudioUnicastServer` / `BLEAudioUnicastClient` / broadcast) mint and own
 * the streams and hand them to the application through their callbacks; the
 * application uses the stream to observe state, receive transparent SDUs, and
 * (for a source stream) send them.
 *
 * Backend-agnostic: this header names no `esp_ble_audio_*` type. The actual
 * stream lives in the BAP engine and is reached through the C-safe
 * `BLEAudioBapVendor` boundary; this handle only carries the direction and the
 * application callbacks.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include <functional>
#include <cstdint>
#include "BTStatus.h"
#include "audio/BLEAudioTypes.h"

class BLEAudioStream {
public:
  using StateCallback = std::function<void(BLEAudioStream &)>;
  using StoppedCallback = std::function<void(BLEAudioStream &, uint8_t reason)>;
  using ReceiveCallback = std::function<void(BLEAudioStream &, const BLEAudioSduInfo &, const uint8_t *sdu, uint16_t len)>;
  using SentCallback = std::function<void(BLEAudioStream &)>;

  BLEAudioStream();
  ~BLEAudioStream() = default;
  BLEAudioStream(const BLEAudioStream &) = default;
  BLEAudioStream &operator=(const BLEAudioStream &) = default;

  /** @brief Whether this handle references a live stream. */
  explicit operator bool() const;

  /** @brief True for a source stream (local transmits); false for a sink stream. */
  bool isSource() const;

  /** @brief Whether the stream is currently in the Streaming state. */
  bool isStreaming() const;

  /**
   * @brief Send one transparent SDU on a source stream.
   *
   * Valid only on a source stream that is streaming. The payload is passed
   * through unchanged (no LC3 encode) -- the turnkey codec pipeline (Phase 3)
   * layers on top of this.
   *
   * @param sdu    SDU payload.
   * @param len    Payload length in octets.
   * @param seqNum Monotonic per-stream SDU sequence number.
   * @return BTStatus::OK on success, or an error code.
   */
  BTStatus write(const uint8_t *sdu, uint16_t len, uint16_t seqNum);

  /** @brief Called when the stream enters the Streaming state. */
  void onStarted(StateCallback cb);
  /** @brief Called when the stream leaves the Streaming state (with a reason). */
  void onStopped(StoppedCallback cb);
  /** @brief Called for each transparent SDU received on a sink stream. */
  void onReceive(ReceiveCallback cb);
  /** @brief Called when a queued SDU has been sent on a source stream. */
  void onSent(SentCallback cb);

  struct Impl;

  // Internal: construct a handle around an existing Impl (used by role handles).
  explicit BLEAudioStream(std::shared_ptr<Impl> impl);
  const std::shared_ptr<Impl> &_implPtr() const {
    return _impl;
  }

private:
  std::shared_ptr<Impl> _impl;
};

#endif /* BLE_AUDIO_SUPPORTED */
