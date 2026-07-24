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
 * @file BLEAudioLc3.c
 * @brief LC3 codec C boundary implementation (see BLEAudioLc3.h).
 *
 * Uses the self-contained direct LC3 API from `esp_audio_codec`
 * (`esp_lc3_enc_*` / `esp_lc3_dec_*`), so no common-registry registration is
 * required and no codec type crosses into C++.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_LC3_SUPPORTED

#include "audio/BLEAudioLc3.h"

#include "esp_lc3_enc.h"
#include "esp_lc3_dec.h"
#include "esp_log.h"

static const char *LC3_TAG = "BLEAudioLc3";

/* LE Audio uses 7.5 ms or 10 ms frames; the codec wants deci-milliseconds. */
static uint8_t frame_dms_from_us(uint32_t frame_dur_us) {
  return (frame_dur_us == 7500) ? 75 : 100;
}

/* ── Encoder ─────────────────────────────────────────────────────────────── */

ble_lc3_enc_t bleLc3EncOpen(uint32_t sample_rate_hz, uint32_t frame_dur_us, uint8_t channels, uint16_t octets_per_frame) {
  esp_lc3_enc_config_t cfg = {
    .sample_rate = sample_rate_hz,
    .bits_per_sample = 16,
    .channel = channels ? channels : 1,
    .frame_dms = frame_dms_from_us(frame_dur_us),
    .nbyte = octets_per_frame,
    .len_prefixed = false,
  };
  void *h = NULL;
  esp_audio_err_t err = esp_lc3_enc_open(&cfg, sizeof(cfg), &h);
  if (err != ESP_AUDIO_ERR_OK) {
    ESP_LOGE(LC3_TAG, "enc_open failed: %d", (int)err);
    return NULL;
  }
  return h;
}

int bleLc3EncFrameSize(ble_lc3_enc_t enc, int *pcm_bytes, int *lc3_bytes) {
  if (!enc) {
    return -1;
  }
  int in_sz = 0, out_sz = 0;
  esp_audio_err_t err = esp_lc3_enc_get_frame_size(enc, &in_sz, &out_sz);
  if (err != ESP_AUDIO_ERR_OK) {
    return (int)err;
  }
  if (pcm_bytes) {
    *pcm_bytes = in_sz;
  }
  if (lc3_bytes) {
    *lc3_bytes = out_sz;
  }
  return 0;
}

int bleLc3EncProcess(ble_lc3_enc_t enc, const uint8_t *pcm, int pcm_len, uint8_t *out, int out_cap) {
  if (!enc || !pcm || !out) {
    return -1;
  }
  esp_audio_enc_in_frame_t in_frame = {
    .buffer = (uint8_t *)pcm,
    .len = (uint32_t)pcm_len,
  };
  esp_audio_enc_out_frame_t out_frame = {
    .buffer = out,
    .len = (uint32_t)out_cap,
  };
  esp_audio_err_t err = esp_lc3_enc_process(enc, &in_frame, &out_frame);
  if (err != ESP_AUDIO_ERR_OK) {
    return -((int)err + 1);
  }
  return (int)out_frame.encoded_bytes;
}

void bleLc3EncClose(ble_lc3_enc_t enc) {
  if (enc) {
    esp_lc3_enc_close(enc);
  }
}

/* ── Decoder ─────────────────────────────────────────────────────────────── */

ble_lc3_dec_t bleLc3DecOpen(uint32_t sample_rate_hz, uint32_t frame_dur_us, uint8_t channels, uint16_t octets_per_frame) {
  esp_lc3_dec_cfg_t cfg = {
    .sample_rate = sample_rate_hz,
    .channel = channels ? channels : 1,
    .bits_per_sample = 16,
    .frame_dms = frame_dms_from_us(frame_dur_us),
    .nbyte = octets_per_frame,
    .is_cbr = true,
    .len_prefixed = false,
    .enable_plc = true,
  };
  void *h = NULL;
  esp_audio_err_t err = esp_lc3_dec_open(&cfg, sizeof(cfg), &h);
  if (err != ESP_AUDIO_ERR_OK) {
    ESP_LOGE(LC3_TAG, "dec_open failed: %d", (int)err);
    return NULL;
  }
  return h;
}

int bleLc3DecFrameSize(ble_lc3_dec_t dec, int *pcm_bytes) {
  /* One decoded frame's PCM size = samples/ch * channels * 2. The decoder
   * reports it via dec_info after the first decode; callers instead derive it
   * from the negotiated params, so this is a convenience that decodes nothing.
   * Kept for symmetry; returns -1 to signal "derive from params". */
  (void)dec;
  if (pcm_bytes) {
    *pcm_bytes = 0;
  }
  return -1;
}

int bleLc3DecProcess(ble_lc3_dec_t dec, const uint8_t *in, int in_len, uint8_t *pcm, int pcm_cap) {
  if (!dec || !in || !pcm) {
    return -1;
  }
  esp_audio_dec_in_raw_t raw = {
    .buffer = (uint8_t *)in,
    .len = (uint32_t)in_len,
    .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
  };
  esp_audio_dec_out_frame_t out_frame = {
    .buffer = pcm,
    .len = (uint32_t)pcm_cap,
  };
  esp_audio_dec_info_t info = {0};
  esp_audio_err_t err = esp_lc3_dec_decode(dec, &raw, &out_frame, &info);
  if (err != ESP_AUDIO_ERR_OK) {
    return -((int)err + 1);
  }
  return (int)out_frame.decoded_size;
}

int bleLc3DecConceal(ble_lc3_dec_t dec, uint8_t *pcm, int pcm_cap) {
  if (!dec || !pcm) {
    return -1;
  }
  /* PLC: input buffer/len are ignored when frame_recover == PLC. */
  esp_audio_dec_in_raw_t raw = {
    .buffer = NULL,
    .len = 0,
    .frame_recover = ESP_AUDIO_DEC_RECOVERY_PLC,
  };
  esp_audio_dec_out_frame_t out_frame = {
    .buffer = pcm,
    .len = (uint32_t)pcm_cap,
  };
  esp_audio_dec_info_t info = {0};
  esp_audio_err_t err = esp_lc3_dec_decode(dec, &raw, &out_frame, &info);
  if (err != ESP_AUDIO_ERR_OK) {
    return -((int)err + 1);
  }
  return (int)out_frame.decoded_size;
}

void bleLc3DecClose(ble_lc3_dec_t dec) {
  if (dec) {
    esp_lc3_dec_close(dec);
  }
}

#endif /* BLE_AUDIO_LC3_SUPPORTED */
