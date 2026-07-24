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
 * @brief C language boundary for the ESP-BLE-AUDIO Coordinated Set Identification
 *        Profile (CSIP).
 *
 * All `esp_ble_audio_csip_*` calls live in the sibling C translation unit
 * (`BLEAudioCsipVendor.c`); the C++ layer reaches this C-safe surface only.
 * Mirrors the VCP/MICP boundaries.
 *
 * Roles wired here:
 *  - Set Member (CSIP server / CSIS): publishes the SIRK, set size, and rank so
 *    a coordinator can discover and (optionally) lock the coordinated set (e.g.
 *    a stereo earbud pair). Exposes RSI generation for set-discovery advertising.
 *  - Set Coordinator (CSIP client): discovers a peer member and reads its set
 *    info (size/rank).
 *
 * The boundary drives a single local member instance and a single coordinator
 * link, matching the ESP-to-ESP coordinated-set demo/validation.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of the Set Identity Resolving Key (SIRK). */
#define BLE_CSIP_VENDOR_SIRK_SIZE 16
/** Size of the Resolvable Set Identifier (RSI). */
#define BLE_CSIP_VENDOR_RSI_SIZE  6

/* ── Dispatch into C++ (set once before init) ───────────────────────────── */

/** Member: lock state changed (by a coordinator, or locally / by timeout). */
typedef void (*ble_csip_vendor_member_lock_fn)(bool locked);

typedef struct {
  ble_csip_vendor_member_lock_fn lock_changed;
} ble_csip_vendor_member_cbs_t;

/** Coordinator: discovery of a peer member completed. */
typedef void (*ble_csip_vendor_coord_disc_fn)(int err, uint8_t set_count, uint8_t set_size, uint8_t rank);

typedef struct {
  ble_csip_vendor_coord_disc_fn discovered;
} ble_csip_vendor_coord_cbs_t;

void bleCsipVendorSetMemberCbs(const ble_csip_vendor_member_cbs_t *cbs);
void bleCsipVendorSetCoordinatorCbs(const ble_csip_vendor_coord_cbs_t *cbs);

/* ── Set Member (CSIP server) ───────────────────────────────────────────── */

/**
 * @brief Register a CSIS instance with the given SIRK/size/rank. Call between
 *        BLEAudio::begin and start.
 * @param sirk     16-octet Set Identity Resolving Key.
 * @param set_size Number of members in the set (0 disables the size char).
 * @param rank     This member's rank (1..set_size, must be unique in the set).
 * @param lockable Whether the set exposes the lock characteristic.
 * @return 0 on success, or an error code.
 */
int bleCsipVendorMemberInit(const uint8_t sirk[BLE_CSIP_VENDOR_SIRK_SIZE], uint8_t set_size, uint8_t rank, bool lockable);

/**
 * @brief Opaque CSIS service instance created by bleCsipVendorMemberInit().
 *
 * Handed to the engine's start info by bleAudioVendorCommonStart(), which is
 * what actually adds the service to the GATT table. A standalone set member is
 * not included by CAS (that instance comes from the CAP acceptor instead).
 *
 * @return the instance, or NULL if no set member is registered.
 */
void *bleCsipVendorMemberCsisInst(void);

/** @brief Update the SIRK at runtime. */
int bleCsipVendorMemberSetSirk(const uint8_t sirk[BLE_CSIP_VENDOR_SIRK_SIZE]);
/** @brief Update the set size and rank at runtime. */
int bleCsipVendorMemberSetSizeAndRank(uint8_t set_size, uint8_t rank);
/** @brief Generate the RSI to advertise for set discovery (6 octets, LE). */
int bleCsipVendorMemberGenerateRsi(uint8_t rsi[BLE_CSIP_VENDOR_RSI_SIZE]);
/** @brief Lock (lock=true) or release the local set instance. */
int bleCsipVendorMemberLock(bool lock, bool force);
/** @brief Latest cached lock state. */
bool bleCsipVendorMemberIsLocked(void);

/* ── Set Coordinator (CSIP client) ──────────────────────────────────────── */

/** @brief Register the coordinator callbacks. Call between begin and start. */
int bleCsipVendorCoordinatorInit(void);
/** @brief Discover the peer's coordinated set(s) over an ACL link. */
int bleCsipVendorCoordinatorDiscover(uint16_t conn_handle);

/** @brief Best-effort teardown of both roles' bookkeeping. */
void bleCsipVendorDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
