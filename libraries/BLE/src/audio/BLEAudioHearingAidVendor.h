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
 * @brief C language boundary for the ESP-BLE-AUDIO Hearing Access Service (HAS).
 *
 * A hearing aid (server) publishes a HAS instance exposing a list of named
 * "presets" (listening programs); a client (e.g. a phone app) discovers the
 * service, reads the presets, and switches the active one. The server side
 * requires CONFIG_BT_HAS (+ a non-zero preset count); the client requires
 * CONFIG_BT_HAS_CLIENT.
 *
 * All `esp_ble_audio_has_*` calls live in the sibling C translation unit
 * (`BLEAudioHearingAidVendor.c`); the C++ layer reaches this surface only.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hearing-aid type values (mirror the spec/engine). */
#define BLE_HAS_VENDOR_TYPE_BINAURAL 0x00
#define BLE_HAS_VENDOR_TYPE_MONAURAL 0x01
#define BLE_HAS_VENDOR_TYPE_BANDED   0x02

/* ── Server dispatch into C++ (set once before init) ────────────────────── */

/** Server: the client requested that preset @p index be selected. */
typedef void (*ble_has_vendor_select_fn)(uint8_t index, bool sync);

void bleHasVendorSetSelectCb(ble_has_vendor_select_fn fn);

/* ── Client dispatch into C++ (set once before init) ────────────────────── */

/** Client: HAS discovery completed (err==0 => `has` cached for later ops). */
typedef void (*ble_has_vendor_disc_fn)(int err, uint8_t type, uint8_t caps);
/** Client: a preset record was read (or notified). */
typedef void (*ble_has_vendor_preset_read_fn)(int err, uint8_t index, bool available, const char *name, bool is_last);
/** Client: the active preset changed on the peer. */
typedef void (*ble_has_vendor_preset_switch_fn)(int err, uint8_t index);

typedef struct {
  ble_has_vendor_disc_fn discovered;
  ble_has_vendor_preset_read_fn preset_read;
  ble_has_vendor_preset_switch_fn preset_switch;
} ble_has_vendor_client_cbs_t;

void bleHasVendorSetClientCbs(const ble_has_vendor_client_cbs_t *cbs);

/* ── Server (hearing aid device) ────────────────────────────────────────── */

/**
 * @brief Register a HAS instance. Call between BLEAudio::begin and start.
 * @param hearing_aid_type One of BLE_HAS_VENDOR_TYPE_*.
 * @param preset_sync Preset synchronization support (binaural sets only).
 * @return 0 on success, or an error code.
 */
int bleHasVendorServerInit(uint8_t hearing_aid_type, bool preset_sync);

/**
 * @brief Register (add) a preset record. Call after bleHasVendorServerInit,
 *        before start.
 * @return 0 on success, or an error code.
 */
int bleHasVendorServerAddPreset(uint8_t index, bool writable, bool available, const char *name);

/** @brief Set the active preset by index. */
int bleHasVendorServerSetActive(uint8_t index);
/** @brief Get the active preset index (0 = none). */
uint8_t bleHasVendorServerGetActive(void);

/* ── Client (hearing aid controller) ────────────────────────────────────── */

/** @brief Register the HAS client callbacks. Call between begin and start. */
int bleHasVendorClientInit(void);
/** @brief Discover a peer's HAS over an ACL link. */
int bleHasVendorClientDiscover(uint16_t conn_handle);
/** @brief Read up to @p max_count presets starting at @p start_index. */
int bleHasVendorClientReadPresets(uint8_t start_index, uint8_t max_count);
/** @brief Set the active preset by index on the peer. */
int bleHasVendorClientSetPreset(uint8_t index, bool sync);
/** @brief Activate the next available preset on the peer. */
int bleHasVendorClientNextPreset(bool sync);
/** @brief Activate the previous available preset on the peer. */
int bleHasVendorClientPrevPreset(bool sync);

/** @brief Best-effort teardown of HAS dispatch bookkeeping. */
void bleHasVendorDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
