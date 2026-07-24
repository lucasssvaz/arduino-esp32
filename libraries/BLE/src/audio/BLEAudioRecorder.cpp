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
 * @file BLEAudioRecorder.cpp
 * @brief Turnkey LC3 capture facade (see BLEAudioRecorder.h).
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_LC3_SUPPORTED

#include "audio/BLEAudioRecorder.h"
#include "audio/BLEAudioPipeline.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp32-hal-log.h"

struct BLEAudioRecorder::Impl {
  BLEAudioStream stream;
  BLEAudioCodecConfig cfg;
  bool useI2s = false;
  BLEAudioI2sConfig i2s;
  BLEAudioPcmSource userSource;

  BLEAudioPipeline pipeline;
  i2s_chan_handle_t rx = nullptr;
  bool running = false;

  uint8_t channels() const {
    return (cfg.channelAllocation == BLEAudioLocation::Mono) ? 1 : 2;
  }

  bool openI2s() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(i2s.port, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, nullptr, &rx) != ESP_OK) {
      log_e("recorder: i2s_new_channel failed");
      return false;
    }
    i2s_slot_mode_t slotMode = (channels() == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg.samplingRateHz),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, slotMode),
      .gpio_cfg = {
        .mclk = (gpio_num_t)i2s.mclk,
        .bclk = (gpio_num_t)i2s.bclk,
        .ws = (gpio_num_t)i2s.ws,
        .dout = I2S_GPIO_UNUSED,
        .din = (gpio_num_t)i2s.din,
        .invert_flags = {0, 0, 0},
      },
    };
    if (i2s_channel_init_std_mode(rx, &std_cfg) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) {
      log_e("recorder: i2s init/enable failed");
      i2s_del_channel(rx);
      rx = nullptr;
      return false;
    }
    return true;
  }

  void closeI2s() {
    if (rx) {
      i2s_channel_disable(rx);
      i2s_del_channel(rx);
      rx = nullptr;
    }
  }
};

BLEAudioRecorder::BLEAudioRecorder() : _impl(nullptr) {}

BLEAudioRecorder::BLEAudioRecorder(BLEAudioStream source, const BLEAudioI2sConfig &i2s, BLEAudioCodecPreset preset)
  : _impl(new Impl()) {
  _impl->stream = source;
  _impl->cfg = BLEAudioCodecConfig::fromPreset(preset);
  _impl->useI2s = true;
  _impl->i2s = i2s;
}

BLEAudioRecorder::BLEAudioRecorder(BLEAudioStream source, BLEAudioPcmSource onPcm, BLEAudioCodecPreset preset)
  : _impl(new Impl()) {
  _impl->stream = source;
  _impl->cfg = BLEAudioCodecConfig::fromPreset(preset);
  _impl->useI2s = false;
  _impl->userSource = std::move(onPcm);
}

BLEAudioRecorder::~BLEAudioRecorder() {
  end();
}

BLEAudioRecorder::BLEAudioRecorder(BLEAudioRecorder &&) noexcept = default;
BLEAudioRecorder &BLEAudioRecorder::operator=(BLEAudioRecorder &&) noexcept = default;

BTStatus BLEAudioRecorder::begin() {
  if (!_impl) {
    return BTStatus::InvalidState;
  }
  if (_impl->running) {
    return BTStatus::OK;
  }
  if (!_impl->stream) {
    log_e("recorder: null source stream");
    return BTStatus::InvalidParam;
  }

  _impl->pipeline.configure(_impl->cfg);

  BLEAudioPipeline::PcmSource source;
  if (_impl->useI2s) {
    if (!_impl->openI2s()) {
      return BTStatus::Fail;
    }
    i2s_chan_handle_t rx = _impl->rx;
    source = [rx](int16_t *pcm, size_t maxSamples) -> size_t {
      size_t read = 0;
      i2s_channel_read(rx, pcm, maxSamples * sizeof(int16_t), &read, pdMS_TO_TICKS(100));
      return read / sizeof(int16_t);
    };
  } else {
    if (!_impl->userSource) {
      return BTStatus::InvalidParam;
    }
    source = _impl->userSource;
  }

  if (!_impl->pipeline.startEncode(_impl->stream, std::move(source))) {
    _impl->closeI2s();
    return BTStatus::Fail;
  }
  _impl->running = true;
  return BTStatus::OK;
}

void BLEAudioRecorder::end() {
  if (!_impl || !_impl->running) {
    return;
  }
  _impl->pipeline.stop();
  _impl->closeI2s();
  _impl->running = false;
}

BLEAudioRecorder::operator bool() const {
  return _impl && _impl->running;
}

#endif /* BLE_AUDIO_LC3_SUPPORTED */
