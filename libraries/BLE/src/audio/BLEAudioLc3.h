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
 * @brief C language boundary for the LC3 codec (managed `esp_audio_codec`).
 *
 * Wraps the direct LC3 encode/decode API (`esp_lc3_enc_*` / `esp_lc3_dec_*`)
 * behind a narrow, C-safe surface so the C++ pipeline never names codec types.
 * The direct API is self-contained (no common-registry registration needed).
 *
 * Only compiled when the LC3 codec is present (see BLE_AUDIO_LC3_SUPPORTED,
 * which detects the `esp_audio_codec` headers). LE Audio always uses 16-bit
 * mono/stereo PCM; this boundary fixes bits-per-sample at 16.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_LC3_SUPPORTED

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque LC3 encoder/decoder handles (real types stay in the .c). */
typedef void *ble_lc3_enc_t;
typedef void *ble_lc3_dec_t;

/**
 * @brief Open an LC3 encoder for the negotiated stream parameters.
 * @param sample_rate_hz   PCM/codec sample rate (e.g. 16000, 24000, 48000).
 * @param frame_dur_us     Frame duration in microseconds (7500 or 10000).
 * @param channels         PCM channel count (1 = mono, 2 = stereo).
 * @param octets_per_frame LC3 octets per channel per frame (the BAP nbyte).
 * @return Encoder handle, or NULL on failure.
 */
ble_lc3_enc_t bleLc3EncOpen(uint32_t sample_rate_hz, uint32_t frame_dur_us, uint8_t channels, uint16_t octets_per_frame);

/**
 * @brief Report the exact PCM input and LC3 output byte counts per frame.
 * @return 0 on success, non-zero on error.
 */
int bleLc3EncFrameSize(ble_lc3_enc_t enc, int *pcm_bytes, int *lc3_bytes);

/**
 * @brief Encode one PCM frame into an LC3 frame.
 * @param pcm     Interleaved 16-bit PCM, exactly the size from bleLc3EncFrameSize.
 * @param out     Destination for the LC3 frame.
 * @param out_cap Capacity of @p out in bytes.
 * @return Encoded byte count (>0), or <=0 on error.
 */
int bleLc3EncProcess(ble_lc3_enc_t enc, const uint8_t *pcm, int pcm_len, uint8_t *out, int out_cap);

/** @brief Close an LC3 encoder. */
void bleLc3EncClose(ble_lc3_enc_t enc);

/**
 * @brief Open an LC3 decoder for the negotiated stream parameters.
 *
 * PLC (packet loss concealment) is always enabled so a lost SDU can be
 * concealed by calling bleLc3DecConceal().
 * @see bleLc3EncOpen for the parameters.
 */
ble_lc3_dec_t bleLc3DecOpen(uint32_t sample_rate_hz, uint32_t frame_dur_us, uint8_t channels, uint16_t octets_per_frame);

/** @brief Report the exact PCM output byte count per decoded frame. */
int bleLc3DecFrameSize(ble_lc3_dec_t dec, int *pcm_bytes);

/**
 * @brief Decode one LC3 frame into PCM.
 * @param in      LC3 frame bytes.
 * @param pcm     Destination for interleaved 16-bit PCM.
 * @param pcm_cap Capacity of @p pcm in bytes.
 * @return Decoded PCM byte count (>0), or <=0 on error.
 */
int bleLc3DecProcess(ble_lc3_dec_t dec, const uint8_t *in, int in_len, uint8_t *pcm, int pcm_cap);

/**
 * @brief Produce one concealed PCM frame for a lost SDU (PLC).
 * @param pcm     Destination for interleaved 16-bit PCM.
 * @param pcm_cap Capacity of @p pcm in bytes.
 * @return Concealed PCM byte count (>0), or <=0 on error.
 */
int bleLc3DecConceal(ble_lc3_dec_t dec, uint8_t *pcm, int pcm_cap);

/** @brief Close an LC3 decoder. */
void bleLc3DecClose(ble_lc3_dec_t dec);

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_LC3_SUPPORTED */
