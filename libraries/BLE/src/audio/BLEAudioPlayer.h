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
 * @brief Turnkey LC3 playback: a sink BLEAudioStream -> LC3 decode -> I2S out.
 *
 * `BLEAudioPlayer` is a direct-constructed RAII facade (like `BLEStream`) that
 * turns received transparent SDUs on a *sink* stream into analog audio: it owns
 * the LC3 decoder, a jitter buffer, a dedicated task, and (optionally) the I2S
 * output channel. Bind it to the sink stream a role handed you (unicast server
 * sink ASE or broadcast sink), point it at your I2S DAC pins, and call
 * `begin()`.
 *
 * Two endpoints are offered:
 *  - **I2S**: pass a @ref BLEAudioI2sConfig and the player drives an I2S DAC.
 *  - **PCM callback**: pass a @ref BLEAudioPcmSink to receive decoded PCM
 *    yourself (custom DSP, a different output device, or a test checksum).
 *
 * The codec preset must match the negotiated stream configuration; use the same
 * preset you configured on the role handle.
 *
 * Only available when the LC3 codec is compiled in (BLE_AUDIO_LC3_SUPPORTED).
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_LC3_SUPPORTED

#include <memory>
#include "BTStatus.h"
#include "audio/BLEAudioStream.h"
#include "audio/BLEAudioTypes.h"
#include "audio/BLEAudioI2s.h"

class BLEAudioPlayer {
public:
  BLEAudioPlayer();

  /**
   * @brief Build an I2S-output player bound to a sink stream.
   * @param sink   The sink stream to decode (from a role handle).
   * @param i2s    I2S DAC pin map (needs @ref BLEAudioI2sConfig::dout).
   * @param preset Codec preset matching the negotiated stream config.
   */
  BLEAudioPlayer(BLEAudioStream sink, const BLEAudioI2sConfig &i2s, BLEAudioCodecPreset preset = BLEAudioCodecPreset::LC3_16_2_1);

  /**
   * @brief Build a PCM-callback player bound to a sink stream.
   * @param sink   The sink stream to decode.
   * @param onPcm  Receives decoded interleaved 16-bit PCM.
   * @param preset Codec preset matching the negotiated stream config.
   */
  BLEAudioPlayer(BLEAudioStream sink, BLEAudioPcmSink onPcm, BLEAudioCodecPreset preset = BLEAudioCodecPreset::LC3_16_2_1);

  ~BLEAudioPlayer();

  // Movable, non-copyable (owns a task + codec + I2S channel).
  BLEAudioPlayer(BLEAudioPlayer &&) noexcept;
  BLEAudioPlayer &operator=(BLEAudioPlayer &&) noexcept;
  BLEAudioPlayer(const BLEAudioPlayer &) = delete;
  BLEAudioPlayer &operator=(const BLEAudioPlayer &) = delete;

  /** @brief Override the jitter-buffer depth (SDUs) before begin(). */
  BLEAudioPlayer &setJitterFrames(uint16_t frames);

  /**
   * @brief Open the endpoint and start decoding.
   * @return BTStatus::OK on success, or an error status.
   */
  BTStatus begin();

  /** @brief Stop decoding and release the task, codec, and I2S channel. */
  void end();

  /** @brief Whether the player is currently running. */
  explicit operator bool() const;

  struct Impl;

private:
  std::unique_ptr<Impl> _impl;
};

#endif /* BLE_AUDIO_LC3_SUPPORTED */
