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
 * @file BLEAudioPlayer.cpp
 * @brief Turnkey LC3 playback facade (see BLEAudioPlayer.h).
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_LC3_SUPPORTED

#include "audio/BLEAudioPlayer.h"
#include "audio/BLEAudioPipeline.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp32-hal-log.h"

struct BLEAudioPlayer::Impl {
  BLEAudioStream stream;
  BLEAudioCodecConfig cfg;
  bool useI2s = false;
  BLEAudioI2sConfig i2s;
  BLEAudioPcmSink userSink;
  uint16_t jitter = 4;

  BLEAudioPipeline pipeline;
  i2s_chan_handle_t tx = nullptr;
  bool running = false;

  uint8_t channels() const {
    return (cfg.channelAllocation == BLEAudioLocation::Mono) ? 1 : 2;
  }

  bool openI2s() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(i2s.port, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, &tx, nullptr) != ESP_OK) {
      log_e("player: i2s_new_channel failed");
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
        .dout = (gpio_num_t)i2s.dout,
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {0, 0, 0},
      },
    };
    if (i2s_channel_init_std_mode(tx, &std_cfg) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) {
      log_e("player: i2s init/enable failed");
      i2s_del_channel(tx);
      tx = nullptr;
      return false;
    }
    return true;
  }

  void closeI2s() {
    if (tx) {
      i2s_channel_disable(tx);
      i2s_del_channel(tx);
      tx = nullptr;
    }
  }
};

BLEAudioPlayer::BLEAudioPlayer() : _impl(nullptr) {}

BLEAudioPlayer::BLEAudioPlayer(BLEAudioStream sink, const BLEAudioI2sConfig &i2s, BLEAudioCodecPreset preset)
  : _impl(new Impl()) {
  _impl->stream = sink;
  _impl->cfg = BLEAudioCodecConfig::fromPreset(preset);
  _impl->useI2s = true;
  _impl->i2s = i2s;
}

BLEAudioPlayer::BLEAudioPlayer(BLEAudioStream sink, BLEAudioPcmSink onPcm, BLEAudioCodecPreset preset) : _impl(new Impl()) {
  _impl->stream = sink;
  _impl->cfg = BLEAudioCodecConfig::fromPreset(preset);
  _impl->useI2s = false;
  _impl->userSink = std::move(onPcm);
}

BLEAudioPlayer::~BLEAudioPlayer() {
  end();
}

BLEAudioPlayer::BLEAudioPlayer(BLEAudioPlayer &&) noexcept = default;
BLEAudioPlayer &BLEAudioPlayer::operator=(BLEAudioPlayer &&) noexcept = default;

BLEAudioPlayer &BLEAudioPlayer::setJitterFrames(uint16_t frames) {
  if (_impl) {
    _impl->jitter = frames;
  }
  return *this;
}

BTStatus BLEAudioPlayer::begin() {
  if (!_impl) {
    return BTStatus::InvalidState;
  }
  if (_impl->running) {
    return BTStatus::OK;
  }
  if (!_impl->stream) {
    log_e("player: null sink stream");
    return BTStatus::InvalidParam;
  }

  _impl->pipeline.configure(_impl->cfg);
  _impl->pipeline.setPrefillFrames(_impl->jitter);

  BLEAudioPipeline::PcmSink sink;
  if (_impl->useI2s) {
    if (!_impl->openI2s()) {
      return BTStatus::Fail;
    }
    i2s_chan_handle_t tx = _impl->tx;
    sink = [tx](const int16_t *pcm, size_t sampleCount) {
      size_t written = 0;
      i2s_channel_write(tx, pcm, sampleCount * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
    };
  } else {
    if (!_impl->userSink) {
      return BTStatus::InvalidParam;
    }
    sink = _impl->userSink;
  }

  if (!_impl->pipeline.startDecode(_impl->stream, std::move(sink))) {
    _impl->closeI2s();
    return BTStatus::Fail;
  }
  _impl->running = true;
  return BTStatus::OK;
}

void BLEAudioPlayer::end() {
  if (!_impl || !_impl->running) {
    return;
  }
  _impl->pipeline.stop();
  _impl->closeI2s();
  _impl->running = false;
}

BLEAudioPlayer::operator bool() const {
  return _impl && _impl->running;
}

#endif /* BLE_AUDIO_LC3_SUPPORTED */
