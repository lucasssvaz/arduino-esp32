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
 * @brief C language boundary for the ESP-BLE-AUDIO Common Audio Profile (CAP).
 *
 * CAP is the coordination layer on top of BAP: it publishes the Common Audio
 * Service (CAS) and (through the acceptor) an included CSIS, and coordinates
 * volume/mute across one or more acceptors from a commander.
 *
 * Roles wired here (single-link scope for ESP-to-ESP demos/validation):
 *  - Acceptor (server): registers CAS + a CSIS instance so the device is a
 *    spec-compliant CAP acceptor. This is the mandatory compliance piece and
 *    supersedes a standalone CSIP set member when CAP is used.
 *  - Initiator (client): discovers CAS on a peer to verify CAP support before
 *    coordinating BAP unicast streams.
 *  - Commander (client): discovers CAS and applies coordinated volume / volume
 *    mute / microphone mute changes to a connected acceptor (ad-hoc set of one).
 *
 * All `esp_ble_audio_cap_*` calls live in the sibling C translation unit
 * (`BLEAudioCapVendor.c`); the C++ layer reaches this C-safe surface only.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of the Set Identity Resolving Key (SIRK). */
#define BLE_CAP_VENDOR_SIRK_SIZE 16

/** Commander operation identifiers, reported to the completion callback. */
typedef enum {
  BLE_CAP_VENDOR_OP_DISCOVER = 0,
  BLE_CAP_VENDOR_OP_VOLUME,
  BLE_CAP_VENDOR_OP_VOLUME_MUTE,
  BLE_CAP_VENDOR_OP_MIC_MUTE,
} ble_cap_vendor_op_t;

/* ── Dispatch into C++ (set once before init) ───────────────────────────── */

/** Initiator: CAS discovery completed (err==0 means CAP supported). */
typedef void (*ble_cap_vendor_initiator_disc_fn)(int err, bool has_csis);

typedef struct {
  ble_cap_vendor_initiator_disc_fn discovered;
} ble_cap_vendor_initiator_cbs_t;

/** Commander: a coordinated procedure completed. */
typedef void (*ble_cap_vendor_commander_op_fn)(ble_cap_vendor_op_t op, int err);

typedef struct {
  ble_cap_vendor_commander_op_fn op_complete;
} ble_cap_vendor_commander_cbs_t;

void bleCapVendorSetInitiatorCbs(const ble_cap_vendor_initiator_cbs_t *cbs);
void bleCapVendorSetCommanderCbs(const ble_cap_vendor_commander_cbs_t *cbs);

/* ── Acceptor (CAP server) ──────────────────────────────────────────────── */

/**
 * @brief Register CAS (plus an included CSIS with the given SIRK/size/rank).
 *        Call between BLEAudio::begin and start.
 * @return 0 on success, or an error code.
 */
int bleCapVendorAcceptorInit(const uint8_t sirk[BLE_CAP_VENDOR_SIRK_SIZE], uint8_t set_size, uint8_t rank, bool lockable);

/**
 * @brief Opaque CSIS service instance created by bleCapVendorAcceptorInit().
 *
 * The engine registers the CSIS *object* here but only adds it to the GATT
 * table when the instance is handed back through the engine's start info, so
 * the coordinated commit in bleAudioVendorCommonStart() must collect it. This
 * is the instance CAS includes, so it is reported as included_by_cas.
 *
 * @return the instance, or NULL if no acceptor is registered.
 */
void *bleCapVendorAcceptorCsisInst(void);

/* ── Initiator (CAP client) ─────────────────────────────────────────────── */

/** @brief Register the initiator callbacks. Call between begin and start. */
int bleCapVendorInitiatorInit(void);
/** @brief Discover CAS on the peer over an ACL link. */
int bleCapVendorInitiatorDiscover(uint16_t conn_handle);

/* ── Commander (CAP client) ─────────────────────────────────────────────── */

/** @brief Register the commander callbacks. Call between begin and start. */
int bleCapVendorCommanderInit(void);
/** @brief Discover CAS on the peer over an ACL link. */
int bleCapVendorCommanderDiscover(uint16_t conn_handle);
/** @brief Set absolute volume (0-255) on the connected acceptor. */
int bleCapVendorCommanderChangeVolume(uint16_t conn_handle, uint8_t volume);
/** @brief Set volume mute state on the connected acceptor. */
int bleCapVendorCommanderChangeVolumeMute(uint16_t conn_handle, bool mute);
/** @brief Set microphone mute state on the connected acceptor. */
int bleCapVendorCommanderChangeMicMute(uint16_t conn_handle, bool mute);

/** @brief Best-effort teardown of all CAP role bookkeeping. */
void bleCapVendorDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
