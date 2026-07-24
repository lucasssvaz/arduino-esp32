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
 * @brief C language boundary for the ESP-BLE-AUDIO Microphone Control Profile (MICP).
 *
 * All `esp_ble_audio_micp_*` / `esp_ble_audio_aics_*` calls live in the sibling
 * C translation unit (`BLEAudioMicVendor.c`); the C++ layer reaches this
 * C-safe surface only. Mirrors the VCP boundary (`BLEAudioVcpVendor.h`).
 *
 * Roles wired here:
 *  - Microphone Device (MICP server): publishes the Microphone Control Service
 *    (+ its Audio Input Control sub-service) with a mute state changeable
 *    locally or by a remote controller.
 *  - Microphone Controller (MICP client): discovers a peer device over an ACL
 *    and drives its mute state.
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

/** Device mute state changed (locally or by a remote controller). */
typedef void (*ble_micp_vendor_dev_mute_fn)(uint8_t mute);

typedef struct {
  ble_micp_vendor_dev_mute_fn mute; /*!< Mute state changed. */
} ble_micp_vendor_dev_cbs_t;

/** Controller discovery of a peer device completed. */
typedef void (*ble_micp_vendor_ctlr_disc_fn)(int err, uint8_t aics_cnt);
/** Controller read/observed the peer device's mute state. */
typedef void (*ble_micp_vendor_ctlr_mute_fn)(int err, uint8_t mute);

typedef struct {
  ble_micp_vendor_ctlr_disc_fn discovered; /*!< Discovery finished. */
  ble_micp_vendor_ctlr_mute_fn mute;       /*!< Remote mute state observed. */
} ble_micp_vendor_ctlr_cbs_t;

void bleMicpVendorSetDeviceCbs(const ble_micp_vendor_dev_cbs_t *cbs);
void bleMicpVendorSetControllerCbs(const ble_micp_vendor_ctlr_cbs_t *cbs);

/* ── Microphone Device (MICP server) ────────────────────────────────────── */

/**
 * @brief Register MICS (plus the compiled-in AICS instance) with the given
 *        initial mute. Call between BLEAudio::begin and start.
 * @param mute Initial mute state (0 = unmuted, 1 = muted).
 * @return 0 on success, or an error code.
 */
int bleMicpVendorDeviceInit(uint8_t mute);

/** @brief Mute (mute=true) or unmute the microphone device. */
int bleMicpVendorDeviceSetMute(bool mute);
/** @brief Disable the mute functionality (spec "mute disabled" state). */
int bleMicpVendorDeviceMuteDisable(void);
/** @brief Latest cached device mute state. */
bool bleMicpVendorDeviceIsMuted(void);

/* ── Microphone Controller (MICP client) ────────────────────────────────── */

/** @brief Register the controller callbacks. Call between begin and start. */
int bleMicpVendorControllerInit(void);

/** @brief Discover the Microphone Control Service on an ACL link. */
int bleMicpVendorControllerDiscover(uint16_t conn_handle);
/** @brief Mute/unmute the remote microphone device. */
int bleMicpVendorControllerSetMute(uint16_t conn_handle, bool mute);
/** @brief Read the remote device's mute state (result via mute callback). */
int bleMicpVendorControllerReadMute(uint16_t conn_handle);

/** @brief Best-effort teardown of both roles' bookkeeping. */
void bleMicpVendorDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
