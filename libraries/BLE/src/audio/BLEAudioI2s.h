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
 * @brief Shared value types for the turnkey LC3 data-plane facades
 *        (BLEAudioPlayer / BLEAudioRecorder).
 *
 * Vendor-free by design: pins are plain GPIO numbers so this public header
 * never names an I2S driver type. The facade `.cpp` translates the config into
 * the standard I2S driver behind the scenes.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_LC3_SUPPORTED

#include <functional>
#include <cstdint>
#include <cstddef>

/** Use for any I2S GPIO field that is not wired. */
static constexpr int BLE_AUDIO_I2S_GPIO_UNUSED = -1;

/**
 * @brief Standard-mode I2S pin map for the turnkey audio facades.
 *
 * The clock (sample rate, 16-bit) is derived from the codec preset, so only the
 * pinout and port are needed here. A player uses @ref dout (data to the DAC);
 * a recorder uses @ref din (data from the ADC/mic). Leave the unused data pin
 * at @ref BLE_AUDIO_I2S_GPIO_UNUSED.
 */
struct BLEAudioI2sConfig {
  int bclk = BLE_AUDIO_I2S_GPIO_UNUSED;  ///< Bit clock (SCLK) GPIO.
  int ws = BLE_AUDIO_I2S_GPIO_UNUSED;    ///< Word select (LRCLK/WS) GPIO.
  int dout = BLE_AUDIO_I2S_GPIO_UNUSED;  ///< Data out GPIO (player -> DAC).
  int din = BLE_AUDIO_I2S_GPIO_UNUSED;   ///< Data in GPIO (recorder <- ADC/mic).
  int mclk = BLE_AUDIO_I2S_GPIO_UNUSED;  ///< Master clock GPIO (optional).
  int port = 0;                          ///< I2S peripheral port (0 or 1).
};

/** Consumer of decoded PCM: interleaved 16-bit, @p sampleCount = frames * channels. */
using BLEAudioPcmSink = std::function<void(const int16_t *pcm, size_t sampleCount)>;

/** Producer of PCM to encode: fill up to @p maxSamples 16-bit samples; return count. */
using BLEAudioPcmSource = std::function<size_t(int16_t *pcm, size_t maxSamples)>;

#endif /* BLE_AUDIO_LC3_SUPPORTED */
