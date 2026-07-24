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
 * @brief C language boundary for the ESP-BLE-AUDIO Media Control Profile (MCP).
 *
 * Roles wired here (single-link scope for ESP-to-ESP demos/validation):
 *  - Server (MCS): starts the engine's turnkey reference media player, which is
 *    exposed as a Media Control Service. No per-track wiring needed for the
 *    demo -- the reference player answers play/pause/etc.
 *  - Client (MCC): discovers a peer's MCS, reads media state, and sends media
 *    control commands (play/pause/stop/next/prev/...).
 *
 * All `esp_ble_audio_*` calls live in the sibling C translation unit
 * (`BLEAudioMediaVendor.c`); the C++ layer reaches this C-safe surface only.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Dispatch into C++ (set once before init) ───────────────────────────── */

/** Client: MCS discovery on a peer completed. */
typedef void (*ble_media_vendor_client_disc_fn)(int err);
/** Client: media state read/notified (see MEDIA_PROXY_STATE_*). */
typedef void (*ble_media_vendor_client_state_fn)(int err, uint8_t state);
/** Client: a command was sent to the peer (echo of the opcode). */
typedef void (*ble_media_vendor_client_cmd_sent_fn)(int err, uint8_t opcode);
/** Client: a command result notification arrived. */
typedef void (*ble_media_vendor_client_cmd_ntf_fn)(int err, uint8_t opcode, uint8_t result);

typedef struct {
  ble_media_vendor_client_disc_fn discovered;
  ble_media_vendor_client_state_fn state;
  ble_media_vendor_client_cmd_sent_fn cmd_sent;
  ble_media_vendor_client_cmd_ntf_fn cmd_ntf;
} ble_media_vendor_client_cbs_t;

void bleMediaVendorSetClientCbs(const ble_media_vendor_client_cbs_t *cbs);

/* ── Server (MCS) ───────────────────────────────────────────────────────── */

/**
 * @brief Start the turnkey reference media player exposed via MCS. Call between
 *        BLEAudio::begin and start.
 * @return 0 on success, or an error code.
 */
int bleMediaVendorServerInit(void);

/* ── Client (MCC) ───────────────────────────────────────────────────────── */

/** @brief Register the MCC callbacks. Call between begin and start. */
int bleMediaVendorClientInit(void);
/** @brief Discover MCS on the peer and (optionally) subscribe to notifications. */
int bleMediaVendorClientDiscover(uint16_t conn_handle, bool subscribe);
/** @brief Read the peer's media state. */
int bleMediaVendorClientReadState(uint16_t conn_handle);
/** @brief Send a media control command (opcode, optional param). */
int bleMediaVendorClientSendCommand(uint16_t conn_handle, uint8_t opcode, bool use_param, int32_t param);

/** @brief Best-effort teardown of client bookkeeping. */
void bleMediaVendorDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
