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
 * @brief C language boundary for the ESP-BLE-AUDIO Volume Control Profile (VCP).
 *
 * The vendor VCP/VOCS/AICS headers pull in the Zephyr bluetooth-audio callback
 * structs (`bt_vcp_vol_rend_cb`, `bt_vcp_vol_ctlr_cb`, ...) and register-param
 * layouts that are simplest to populate from C. All of that -- the renderer
 * (Volume Control Service, with its included Volume Offset Control Service and
 * Audio Input Control Service instances) registration, the controller
 * discover/read/set orchestration, and the renderer/controller callback
 * plumbing -- therefore lives in ONE C translation unit (`BLEAudioVcpVendor.c`).
 * The C++ layer above reaches this narrow, C-safe surface only. Mirrors the BAP
 * boundary (`BLEAudioBapVendor.h`); it is a language boundary, not a shim.
 *
 * Roles wired here:
 *  - Volume Renderer (VCP server): publishes VCS (+VOCS/AICS) and exposes the
 *    local volume/mute, changeable locally or by a remote controller.
 *  - Volume Controller (VCP client): discovers a peer renderer over an ACL and
 *    drives its volume/mute.
 *
 * The boundary drives a single renderer and a single controller link, matching
 * the ESP-to-ESP control demo/validation.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Dispatch into C++ (set once before init) ───────────────────────────── */

/** Renderer volume/mute changed (locally or by a remote controller). */
typedef void (*ble_vcp_vendor_rend_state_fn)(uint8_t volume, uint8_t mute);

typedef struct {
  ble_vcp_vendor_rend_state_fn state; /*!< Volume state changed. */
} ble_vcp_vendor_rend_cbs_t;

/** Controller discovery of a peer renderer completed. */
typedef void (*ble_vcp_vendor_ctlr_disc_fn)(int err, uint8_t vocs_cnt, uint8_t aics_cnt);
/** Controller read/observed the peer renderer's volume state. */
typedef void (*ble_vcp_vendor_ctlr_state_fn)(int err, uint8_t volume, uint8_t mute);

typedef struct {
  ble_vcp_vendor_ctlr_disc_fn discovered; /*!< Discovery finished. */
  ble_vcp_vendor_ctlr_state_fn state;     /*!< Remote volume state observed. */
} ble_vcp_vendor_ctlr_cbs_t;

/** @brief Register the C++ dispatch callbacks (call before init). */
void bleVcpVendorSetRendererCbs(const ble_vcp_vendor_rend_cbs_t *cbs);
void bleVcpVendorSetControllerCbs(const ble_vcp_vendor_ctlr_cbs_t *cbs);

/* ── Volume Renderer (VCP server) ───────────────────────────────────────── */

/**
 * @brief Register VCS (plus the compiled-in VOCS/AICS instances) with the given
 *        initial state. Call after `esp_ble_audio_common_init` and before
 *        `esp_ble_audio_common_start` (i.e. between BLEAudio::begin and start).
 *
 * @param volume Initial absolute volume (0-255).
 * @param mute   Initial mute state (0 = unmuted, 1 = muted).
 * @param step   Relative volume step size (1-255).
 * @return 0 on success, or an error code.
 */
int bleVcpVendorRendererInit(uint8_t volume, uint8_t mute, uint8_t step);

/** @brief Set the renderer's absolute volume (0-255). */
int bleVcpVendorRendererSetVolume(uint8_t volume);
/** @brief Mute (mute=true) or unmute the renderer. */
int bleVcpVendorRendererSetMute(bool mute);
/** @brief Step the renderer volume up by the configured step. */
int bleVcpVendorRendererVolumeUp(void);
/** @brief Step the renderer volume down by the configured step. */
int bleVcpVendorRendererVolumeDown(void);
/** @brief Latest cached renderer volume (updated by the state callback). */
uint8_t bleVcpVendorRendererGetVolume(void);
/** @brief Latest cached renderer mute state. */
bool bleVcpVendorRendererIsMuted(void);

/* ── Volume Controller (VCP client) ─────────────────────────────────────── */

/** @brief Register the controller callbacks. Call between begin and start. */
int bleVcpVendorControllerInit(void);

/**
 * @brief Discover the Volume Control Service on an established ACL link.
 * @param conn_handle ACL connection handle to the renderer.
 * @return 0 if discovery started, or an error code.
 */
int bleVcpVendorControllerDiscover(uint16_t conn_handle);

/** @brief Set the remote renderer's absolute volume (0-255). */
int bleVcpVendorControllerSetVolume(uint16_t conn_handle, uint8_t volume);
/** @brief Mute/unmute the remote renderer. */
int bleVcpVendorControllerSetMute(uint16_t conn_handle, bool mute);
/** @brief Step the remote renderer volume up. */
int bleVcpVendorControllerVolumeUp(uint16_t conn_handle);
/** @brief Step the remote renderer volume down. */
int bleVcpVendorControllerVolumeDown(uint16_t conn_handle);
/** @brief Read the remote renderer's volume state (result via state callback). */
int bleVcpVendorControllerReadState(uint16_t conn_handle);

/** @brief Best-effort teardown of both roles' bookkeeping. */
void bleVcpVendorDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
