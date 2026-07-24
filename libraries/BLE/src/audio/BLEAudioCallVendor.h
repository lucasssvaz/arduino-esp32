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
 * @brief C language boundary for the ESP-BLE-AUDIO Call Control Profile (CCP),
 *        which is built on the (Generic) Telephone Bearer Service (TBS/GTBS).
 *
 * Roles wired here (single-link scope for ESP-to-ESP demos/validation):
 *  - Server (CCP / GTBS): publishes a Generic Telephone Bearer so a client can
 *    observe and control calls. Exposes a minimal call lifecycle (announce an
 *    incoming call, terminate) plus originate/terminate request callbacks.
 *  - Client (CCP / TBS client): discovers a peer's GTBS and drives calls
 *    (originate/accept/terminate).
 *
 * The current engine only supports the Generic Telephone Bearer (GTBS), so the
 * bearer index is always the GTBS index.
 *
 * All `esp_ble_audio_*` calls live in the sibling C translation unit
 * (`BLEAudioCallVendor.c`); the C++ layer reaches this C-safe surface only.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Client call-control operation identifiers, reported to callbacks. */
typedef enum {
  BLE_CALL_VENDOR_OP_ORIGINATE = 0,
  BLE_CALL_VENDOR_OP_ACCEPT,
  BLE_CALL_VENDOR_OP_TERMINATE,
  BLE_CALL_VENDOR_OP_HOLD,
  BLE_CALL_VENDOR_OP_RETRIEVE,
} ble_call_vendor_op_t;

/* ── Dispatch into C++ (set once before init) ───────────────────────────── */

/** Server: a client requested to originate a call to @p uri. Return accept. */
typedef bool (*ble_call_vendor_server_originate_fn)(uint8_t call_index, const char *uri);
/** Server: a call was terminated (by client or server). */
typedef void (*ble_call_vendor_server_terminate_fn)(uint8_t call_index, uint8_t reason);

typedef struct {
  ble_call_vendor_server_originate_fn originate;
  ble_call_vendor_server_terminate_fn terminated;
} ble_call_vendor_server_cbs_t;

/** Client: GTBS/TBS discovery on a peer completed. */
typedef void (*ble_call_vendor_client_disc_fn)(int err, uint8_t tbs_count, bool gtbs_found);
/** Client: a call-control operation completed. */
typedef void (*ble_call_vendor_client_op_fn)(ble_call_vendor_op_t op, int err, uint8_t call_index);

typedef struct {
  ble_call_vendor_client_disc_fn discovered;
  ble_call_vendor_client_op_fn op_complete;
} ble_call_vendor_client_cbs_t;

void bleCallVendorSetServerCbs(const ble_call_vendor_server_cbs_t *cbs);
void bleCallVendorSetClientCbs(const ble_call_vendor_client_cbs_t *cbs);

/* ── Server (CCP / GTBS) ────────────────────────────────────────────────── */

/**
 * @brief Register a Generic Telephone Bearer with the given identity. Call
 *        between BLEAudio::begin and start.
 * @param provider_name Bearer provider name (e.g. "ESP Phone").
 * @param uci           Uniform Caller Identifier (e.g. "un000").
 * @return 0 on success, or an error code.
 */
int bleCallVendorServerInit(const char *provider_name, const char *uci);
/** @brief Announce an incoming call from @p from; returns the call index. */
int bleCallVendorServerIncoming(const char *from, uint8_t *call_index);
/** @brief Terminate a call by index. */
int bleCallVendorServerTerminate(uint8_t call_index);
/** @brief Update the bearer provider name at runtime. */
int bleCallVendorServerSetProviderName(const char *name);

/* ── Client (CCP / TBS client) ──────────────────────────────────────────── */

/** @brief Register the client callbacks. Call between begin and start. */
int bleCallVendorClientInit(void);
/** @brief Discover GTBS/TBS on the peer over an ACL link. */
int bleCallVendorClientDiscover(uint16_t conn_handle);
/** @brief Originate a call to @p uri on the peer's GTBS. */
int bleCallVendorClientOriginate(uint16_t conn_handle, const char *uri);
/** @brief Accept an incoming call by index on the peer's GTBS. */
int bleCallVendorClientAccept(uint16_t conn_handle, uint8_t call_index);
/** @brief Terminate a call by index on the peer's GTBS. */
int bleCallVendorClientTerminate(uint16_t conn_handle, uint8_t call_index);

/** @brief Best-effort teardown of all CCP role bookkeeping. */
void bleCallVendorDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
