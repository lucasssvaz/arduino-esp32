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
 * @brief Internal LC3 <-> PCM data-plane engine bound to a BLEAudioStream.
 *
 * A pipeline owns one dedicated FreeRTOS task plus the LC3 codec and moves
 * audio between a `BLEAudioStream` (transparent SDUs on ISO) and PCM:
 *
 *  - **decode** (sink): stream `onReceive` -> jitter queue -> LC3 decode (with
 *    PLC on lost/gapped SDUs) -> PCM sink callback. A configurable prefill
 *    depth realises the presentation-delay jitter buffer.
 *  - **encode** (source): PCM source callback -> LC3 encode -> `stream.write()`,
 *    paced at the codec's SDU interval; short reads are padded with silence.
 *
 * The PCM endpoints are `std::function`s, so the turnkey `BLEAudioPlayer` /
 * `BLEAudioRecorder` facades bind them to I2S while tests bind a tone
 * generator / checksum sink. This class is internal (not part of the public
 * API) and only compiled when the LC3 codec is available.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_LC3_SUPPORTED

#include <functional>
#include <cstdint>
#include <cstddef>

#include "audio/BLEAudioStream.h"
#include "audio/BLEAudioTypes.h"
#include "audio/BLEAudioLc3.h"

class BLEAudioPipeline {
public:
  /** PCM sink: interleaved 16-bit PCM, @p sampleCount = frames * channels. */
  using PcmSink = std::function<void(const int16_t *pcm, size_t sampleCount)>;
  /** PCM source: fill up to @p maxSamples interleaved 16-bit samples; return count produced. */
  using PcmSource = std::function<size_t(int16_t *pcm, size_t maxSamples)>;

  BLEAudioPipeline();
  ~BLEAudioPipeline();

  BLEAudioPipeline(const BLEAudioPipeline &) = delete;
  BLEAudioPipeline &operator=(const BLEAudioPipeline &) = delete;

  /** @brief Set the LC3/PCM parameters (call before start*). */
  void configure(const BLEAudioCodecConfig &cfg);

  /** @brief Jitter-buffer depth: SDUs queued before decode output begins. */
  void setPrefillFrames(uint16_t frames);

  /**
   * @brief Start the decode (sink) pipeline on a sink stream.
   * @param stream A sink stream; its `onReceive` is bound by the pipeline.
   * @param sink   Consumer of decoded PCM (e.g. I2S write).
   * @return true if the codec + task started.
   */
  bool startDecode(BLEAudioStream stream, PcmSink sink);

  /**
   * @brief Start the encode (source) pipeline on a source stream.
   * @param stream A source stream that is (or will be) streaming.
   * @param source Producer of PCM to encode (e.g. I2S read).
   * @return true if the codec + task started.
   */
  bool startEncode(BLEAudioStream stream, PcmSource source);

  /** @brief Stop the task, close the codec, and unbind the stream. */
  void stop();

  /** @brief Whether the pipeline task is running. */
  bool running() const {
    return _run;
  }

  /** @brief PCM bytes per frame (samples/ch * channels * 2). 0 until configured+started. */
  int pcmFrameBytes() const {
    return _pcmBytes;
  }

private:
  struct SduItem;

  static void taskTrampoline(void *arg);
  void decodeLoop();
  void encodeLoop();

  BLEAudioCodecConfig _cfg;
  uint8_t _channels = 1;
  uint16_t _prefill = 4;

  BLEAudioStream _stream;
  PcmSink _sink;
  PcmSource _source;

  ble_lc3_enc_t _enc = nullptr;
  ble_lc3_dec_t _dec = nullptr;

  int _pcmBytes = 0;   // PCM bytes per frame
  int _lc3Bytes = 0;   // LC3 octets per frame (encoder side)

  void *_queue = nullptr;  // QueueHandle_t (decode path only)
  void *_task = nullptr;   // TaskHandle_t
  volatile bool _run = false;
  bool _encoding = false;
};

#endif /* BLE_AUDIO_LC3_SUPPORTED */
