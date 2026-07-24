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
 * @brief Turnkey LC3 capture: I2S in -> LC3 encode -> a source BLEAudioStream.
 *
 * `BLEAudioRecorder` is a direct-constructed RAII facade (like `BLEStream`)
 * that captures PCM, LC3-encodes it, and streams the frames as transparent
 * SDUs on a *source* stream, paced at the codec's SDU interval. Bind it to the
 * source stream a role handed you (unicast client/server source ASE or
 * broadcast source), point it at your I2S mic/ADC pins, and call `begin()`.
 *
 * Two endpoints are offered:
 *  - **I2S**: pass a @ref BLEAudioI2sConfig and the recorder reads an I2S ADC.
 *  - **PCM callback**: pass a @ref BLEAudioPcmSource to supply PCM yourself
 *    (a generator, a different capture device, or a test tone).
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

class BLEAudioRecorder {
public:
  BLEAudioRecorder();

  /**
   * @brief Build an I2S-input recorder bound to a source stream.
   * @param source The source stream to feed (from a role handle).
   * @param i2s    I2S ADC/mic pin map (needs @ref BLEAudioI2sConfig::din).
   * @param preset Codec preset matching the negotiated stream config.
   */
  BLEAudioRecorder(
    BLEAudioStream source, const BLEAudioI2sConfig &i2s, BLEAudioCodecPreset preset = BLEAudioCodecPreset::LC3_16_2_1
  );

  /**
   * @brief Build a PCM-callback recorder bound to a source stream.
   * @param source The source stream to feed.
   * @param onPcm  Supplies interleaved 16-bit PCM to encode.
   * @param preset Codec preset matching the negotiated stream config.
   */
  BLEAudioRecorder(
    BLEAudioStream source, BLEAudioPcmSource onPcm, BLEAudioCodecPreset preset = BLEAudioCodecPreset::LC3_16_2_1
  );

  ~BLEAudioRecorder();

  // Movable, non-copyable (owns a task + codec + I2S channel).
  BLEAudioRecorder(BLEAudioRecorder &&) noexcept;
  BLEAudioRecorder &operator=(BLEAudioRecorder &&) noexcept;
  BLEAudioRecorder(const BLEAudioRecorder &) = delete;
  BLEAudioRecorder &operator=(const BLEAudioRecorder &) = delete;

  /**
   * @brief Open the endpoint and start capturing/encoding.
   * @return BTStatus::OK on success, or an error status.
   */
  BTStatus begin();

  /** @brief Stop capturing and release the task, codec, and I2S channel. */
  void end();

  /** @brief Whether the recorder is currently running. */
  explicit operator bool() const;

  struct Impl;

private:
  std::unique_ptr<Impl> _impl;
};

#endif /* BLE_AUDIO_LC3_SUPPORTED */
