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

/**
 * @file BLEAudioPipeline.cpp
 * @brief LC3 <-> PCM data-plane engine (see BLEAudioPipeline.h).
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_LC3_SUPPORTED

#include "audio/BLEAudioPipeline.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp32-hal-log.h"

#include <cstring>

// Largest LC3 frame we buffer per SDU (48 kHz 10 ms high-rate mono is 155
// octets; 256 covers that plus small multi-channel/framed headroom).
static constexpr uint16_t PIPELINE_SDU_MAX = 256;
// Jitter queue depth (SDUs). At 10 ms/frame this is ~160 ms of buffering.
static constexpr uint16_t PIPELINE_QUEUE_DEPTH = 16;
// Cap on consecutive concealed frames when a sequence-number gap is seen, so a
// long dropout does not stall the task producing thousands of PLC frames.
static constexpr uint16_t PIPELINE_MAX_CONCEAL = 8;

struct BLEAudioPipeline::SduItem {
  uint16_t seq;
  uint16_t len;
  uint8_t valid;
  uint8_t data[PIPELINE_SDU_MAX];
};

static uint8_t channelsOf(BLEAudioLocation loc) {
  if (loc == BLEAudioLocation::Mono) {
    return 1;
  }
  uint32_t bits = static_cast<uint32_t>(loc);
  uint8_t n = 0;
  while (bits) {
    n += (bits & 1u);
    bits >>= 1;
  }
  return n ? n : 1;
}

BLEAudioPipeline::BLEAudioPipeline() {}

BLEAudioPipeline::~BLEAudioPipeline() {
  stop();
}

void BLEAudioPipeline::configure(const BLEAudioCodecConfig &cfg) {
  _cfg = cfg;
  _channels = channelsOf(cfg.channelAllocation);
  uint32_t samplesPerFrame = (uint32_t)((uint64_t)cfg.samplingRateHz * cfg.frameDurationUs / 1000000ull);
  _pcmBytes = (int)(samplesPerFrame * _channels * sizeof(int16_t));
  _lc3Bytes = (int)((uint32_t)cfg.octetsPerFrame * _channels);
}

void BLEAudioPipeline::setPrefillFrames(uint16_t frames) {
  _prefill = frames;
}

// ---------------------------------------------------------------------------
// Decode (sink) path
// ---------------------------------------------------------------------------

bool BLEAudioPipeline::startDecode(BLEAudioStream stream, PcmSink sink) {
  if (_run || !stream || !sink) {
    return false;
  }
  _dec = bleLc3DecOpen(_cfg.samplingRateHz, _cfg.frameDurationUs, _channels, _cfg.octetsPerFrame);
  if (!_dec) {
    log_e("pipeline: LC3 decoder open failed");
    return false;
  }
  _queue = xQueueCreate(PIPELINE_QUEUE_DEPTH, sizeof(SduItem));
  if (!_queue) {
    bleLc3DecClose(_dec);
    _dec = nullptr;
    return false;
  }
  _stream = stream;
  _sink = std::move(sink);
  _encoding = false;
  _run = true;

  // Marshal each received SDU into the jitter queue from the host task.
  QueueHandle_t q = static_cast<QueueHandle_t>(_queue);
  _stream.onReceive([q](BLEAudioStream &, const BLEAudioSduInfo &info, const uint8_t *sdu, uint16_t len) {
    SduItem item;
    item.seq = info.packetSeqNum;
    item.valid = (info.packetStatus == 0) ? 1 : 0;
    item.len = (len > PIPELINE_SDU_MAX) ? PIPELINE_SDU_MAX : len;
    if (item.valid && sdu && item.len) {
      memcpy(item.data, sdu, item.len);
    } else {
      item.len = 0;
    }
    // Non-blocking: if the consumer stalls, drop rather than block the host task.
    xQueueSend(q, &item, 0);
  });

  if (xTaskCreate(taskTrampoline, "ble_lc3_dec", 4096, this, 5, reinterpret_cast<TaskHandle_t *>(&_task)) != pdPASS) {
    log_e("pipeline: decode task create failed");
    _run = false;
    _stream.onReceive(nullptr);
    vQueueDelete(q);
    _queue = nullptr;
    bleLc3DecClose(_dec);
    _dec = nullptr;
    return false;
  }
  log_i("pipeline: decode started (pcm=%d B/frame, %u ch)", _pcmBytes, _channels);
  return true;
}

void BLEAudioPipeline::decodeLoop() {
  QueueHandle_t q = static_cast<QueueHandle_t>(_queue);
  int16_t *pcm = static_cast<int16_t *>(malloc(_pcmBytes));
  if (!pcm) {
    log_e("pipeline: decode PCM buffer alloc failed");
    return;
  }
  size_t pcmSamples = _pcmBytes / sizeof(int16_t);

  // Presentation-delay jitter buffer: wait for a prefill before emitting.
  while (_run && uxQueueMessagesWaiting(q) < _prefill) {
    vTaskDelay(pdMS_TO_TICKS(2));
  }

  bool haveSeq = false;
  uint16_t lastSeq = 0;

  while (_run) {
    SduItem item;
    if (xQueueReceive(q, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }

    // Conceal any missing SDUs between the last decoded seq and this one.
    if (haveSeq) {
      uint16_t gap = (uint16_t)(item.seq - lastSeq - 1);
      if (gap > PIPELINE_MAX_CONCEAL) {
        gap = PIPELINE_MAX_CONCEAL;
      }
      for (uint16_t i = 0; i < gap && _run; i++) {
        int n = bleLc3DecConceal(_dec, reinterpret_cast<uint8_t *>(pcm), _pcmBytes);
        if (n > 0 && _sink) {
          _sink(pcm, n / sizeof(int16_t));
        }
      }
    }

    int n;
    if (item.valid && item.len) {
      n = bleLc3DecProcess(_dec, item.data, item.len, reinterpret_cast<uint8_t *>(pcm), _pcmBytes);
    } else {
      n = bleLc3DecConceal(_dec, reinterpret_cast<uint8_t *>(pcm), _pcmBytes);
    }
    if (n > 0 && _sink) {
      _sink(pcm, n / sizeof(int16_t));
    } else if (n <= 0) {
      // Keep audio flowing on a decode error by emitting silence.
      memset(pcm, 0, _pcmBytes);
      if (_sink) {
        _sink(pcm, pcmSamples);
      }
    }

    lastSeq = item.seq;
    haveSeq = true;
  }

  free(pcm);
}

// ---------------------------------------------------------------------------
// Encode (source) path
// ---------------------------------------------------------------------------

bool BLEAudioPipeline::startEncode(BLEAudioStream stream, PcmSource source) {
  if (_run || !stream || !source) {
    return false;
  }
  _enc = bleLc3EncOpen(_cfg.samplingRateHz, _cfg.frameDurationUs, _channels, _cfg.octetsPerFrame);
  if (!_enc) {
    log_e("pipeline: LC3 encoder open failed");
    return false;
  }
  int pcmSz = 0, lc3Sz = 0;
  if (bleLc3EncFrameSize(_enc, &pcmSz, &lc3Sz) == 0 && pcmSz > 0) {
    _pcmBytes = pcmSz;
    _lc3Bytes = lc3Sz;
  }
  _stream = stream;
  _source = std::move(source);
  _encoding = true;
  _run = true;

  if (xTaskCreate(taskTrampoline, "ble_lc3_enc", 4096, this, 5, reinterpret_cast<TaskHandle_t *>(&_task)) != pdPASS) {
    log_e("pipeline: encode task create failed");
    _run = false;
    bleLc3EncClose(_enc);
    _enc = nullptr;
    return false;
  }
  log_i("pipeline: encode started (pcm=%d B/frame, lc3=%d B, %u ch)", _pcmBytes, _lc3Bytes, _channels);
  return true;
}

void BLEAudioPipeline::encodeLoop() {
  int16_t *pcm = static_cast<int16_t *>(malloc(_pcmBytes));
  uint8_t *lc3 = static_cast<uint8_t *>(malloc(_lc3Bytes > 0 ? _lc3Bytes : PIPELINE_SDU_MAX));
  if (!pcm || !lc3) {
    log_e("pipeline: encode buffer alloc failed");
    free(pcm);
    free(lc3);
    return;
  }
  size_t pcmSamples = _pcmBytes / sizeof(int16_t);
  int lc3Cap = _lc3Bytes > 0 ? _lc3Bytes : PIPELINE_SDU_MAX;

  TickType_t interval = pdMS_TO_TICKS(_cfg.frameDurationUs / 1000);
  if (interval < 1) {
    interval = 1;
  }
  TickType_t last = xTaskGetTickCount();
  uint16_t seq = 0;

  while (_run) {
    vTaskDelayUntil(&last, interval);
    if (!_run) {
      break;
    }

    size_t got = _source ? _source(pcm, pcmSamples) : 0;
    if (got < pcmSamples) {
      memset(pcm + got, 0, (pcmSamples - got) * sizeof(int16_t));  // pad with silence
    }

    if (!_stream.isStreaming()) {
      continue;  // link not up yet; keep pacing but don't send
    }

    int n = bleLc3EncProcess(_enc, reinterpret_cast<uint8_t *>(pcm), _pcmBytes, lc3, lc3Cap);
    if (n > 0) {
      _stream.write(lc3, (uint16_t)n, seq++);
    }
  }

  free(pcm);
  free(lc3);
}

// ---------------------------------------------------------------------------

void BLEAudioPipeline::taskTrampoline(void *arg) {
  BLEAudioPipeline *self = static_cast<BLEAudioPipeline *>(arg);
  if (self->_encoding) {
    self->encodeLoop();
  } else {
    self->decodeLoop();
  }
  self->_task = nullptr;
  vTaskDelete(nullptr);
}

void BLEAudioPipeline::stop() {
  if (!_run && !_enc && !_dec && !_queue) {
    return;
  }
  _run = false;

  // Unbind the stream callback first so no more SDUs are queued.
  if (_stream && !_encoding) {
    _stream.onReceive(nullptr);
  }

  // Wait for the task to exit (it polls _run at <=100 ms granularity).
  for (int i = 0; i < 50 && _task != nullptr; i++) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (_queue) {
    vQueueDelete(static_cast<QueueHandle_t>(_queue));
    _queue = nullptr;
  }
  if (_enc) {
    bleLc3EncClose(_enc);
    _enc = nullptr;
  }
  if (_dec) {
    bleLc3DecClose(_dec);
    _dec = nullptr;
  }
  _sink = nullptr;
  _source = nullptr;
  _stream = BLEAudioStream();
}

#endif /* BLE_AUDIO_LC3_SUPPORTED */
