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
 * @brief C language boundary for the ESP-BLE-AUDIO BAP Broadcast Source.
 *
 * Sibling of `BLEAudioBapVendor.h` (unicast). The vendor broadcast APIs use the
 * same GNU-C codec/preset macros and Zephyr structs, so all of the broadcast
 * source setup (`esp_ble_audio_bap_broadcast_source_*`, BASE encoding, the BIG
 * carrier attach) lives in one C translation unit and the C++ layer talks to
 * this narrow, C-safe surface only.
 *
 * Scope (Phase 2b): a single-subgroup, single-stream (mono) Broadcast Source --
 * spec-valid and matching the ESP-to-ESP Auracast demo/validation. The data
 * plane is transparent SDUs; LC3/I2S layers on top later without touching this
 * file. The stream callbacks reuse `ble_bap_vendor_stream_cbs_t`
 * (from BLEAudioBapVendor.h): the broadcast source is transmit-only, so only
 * the SOURCE direction is dispatched.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "audio/BLEAudioBapVendor.h"  // ble_bap_vendor_preset_t + stream cbs types

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Register the C++ dispatch callbacks for the broadcast source stream. */
void bleBapBroadcastVendorSetStreamCbs(const ble_bap_vendor_stream_cbs_t *cbs);

/**
 * @brief Create the Broadcast Source (one subgroup, one mono stream).
 *
 * Must be called between `esp_ble_audio_common_init` and the advertising set-up
 * (i.e. between BLEAudio::begin and the role start), because the BASE that goes
 * into the periodic advertising data is only available once the source exists.
 *
 * @param preset   LC3 broadcast preset for the stream.
 * @param encrypt  Encrypt the BIG with @p code.
 * @param code     Broadcast code (max 16 octets); ignored when !encrypt.
 * @param code_len Length of @p code in octets.
 * @return 0 on success, or an error code.
 */
int bleBapBroadcastSourceCreate(ble_bap_vendor_preset_t preset, bool encrypt, const uint8_t *code, uint8_t code_len);

/**
 * @brief Copy the encoded BASE (Broadcast Audio Source Endpoint) for the
 *        periodic-advertising Basic Audio Announcement.
 *
 * The returned bytes already include the leading 16-bit service UUID, so the
 * caller wraps them directly as a Service Data - 16 bit AD structure.
 *
 * @param out     Destination buffer.
 * @param cap     Capacity of @p out in octets.
 * @param out_len Set to the number of octets written.
 * @return 0 on success, or an error code (e.g. buffer too small / no source).
 */
int bleBapBroadcastSourceGetBase(uint8_t *out, uint16_t cap, uint16_t *out_len);

/**
 * @brief Attach the (already-advertising) periodic-adv carrier and start streaming.
 *
 * The extended + periodic advertising for @p adv_handle must already be running
 * (ext data = Broadcast Audio Announcement, periodic data = BASE) before this
 * call. Adds the carrier to the source and starts the BIG.
 *
 * @param adv_handle Advertising instance handle carrying the periodic train.
 * @return 0 on success, or an error code.
 */
int bleBapBroadcastSourceStart(uint8_t adv_handle);

/** @brief Stop the BIG (streams stop; the source object is retained). */
int bleBapBroadcastSourceStop(void);

/** @brief Delete the source object and reset all broadcast state. */
void bleBapBroadcastSourceDelete(void);

/**
 * @brief Send one transparent SDU on the broadcast source stream.
 * @param seq_num Monotonic per-stream SDU sequence number.
 * @return 0 on success, or an error code (e.g. not streaming / no credit).
 */
int bleBapBroadcastStreamSend(const uint8_t *sdu, uint16_t len, uint16_t seq_num);

/** @brief Whether the broadcast source stream is in the Streaming state. */
bool bleBapBroadcastStreamIsStreaming(void);

/* ── Broadcast Sink (Auracast receiver) + Scan Delegator (BASS) ──────────── */

/**
 * @brief PA-sync request callback: the vendor asks the C++/scan layer to create
 *        a periodic-advertising sync to a broadcast source.
 *
 * Invoked when a remote Broadcast Assistant (over BASS) requests the sink to
 * sync to a source it added. The self-initiated path (the sink's own scan
 * matching a target) creates the sync directly in C++ and does not use this.
 */
/**
 * @brief PA-sync request callback: the vendor asks the C++/scan layer to create
 *        a periodic-advertising sync to a broadcast source, or to enable PAST.
 *
 * Invoked when a remote Broadcast Assistant (over BASS) requests the sink to
 * sync to a source it added. @p past_available true means the assistant will
 * transfer the PA sync over the ACL; enable PAST receive on @p conn_handle.
 * Pass @p addr NULL to cancel a previously enabled PAST receive.
 * The self-initiated path (the sink's own scan matching a target) creates the
 * sync directly in C++ and does not use this.
 *
 * @return 0 on success, negative errno on failure.
 */
typedef int (*ble_bap_bcast_pa_sync_req_fn)(
  uint8_t addr_type, const uint8_t addr[6], uint8_t sid, uint32_t broadcast_id, bool past_available, uint16_t conn_handle
);

/** @brief Register the PA-sync request callback (assistant-driven flow). */
void bleBapBroadcastSinkSetPaSyncReqFn(ble_bap_bcast_pa_sync_req_fn fn);

/**
 * @brief Initialize the Broadcast Sink: register PACS sink capability, the Scan
 *        Delegator (BASS) service, the broadcast-sink callbacks, and hook the
 *        engine GAP observer. Call between BLEAudio::begin and start.
 *
 * @param preset   LC3 preset the sink advertises support for.
 * @param encrypt  The expected BIG is encrypted (uses @p code on sync).
 * @param code     Broadcast code (max 16 octets); ignored when !encrypt.
 * @param code_len Length of @p code in octets.
 * @return 0 on success, or an error code.
 */
int bleBapBroadcastSinkInit(ble_bap_vendor_preset_t preset, bool encrypt, const uint8_t *code, uint8_t code_len);

/**
 * @brief Set the broadcast ID the next PA sync should create a sink for.
 *
 * The self-initiated scanner calls this with the matched source's broadcast ID
 * just before creating the periodic sync, so the PA_SYNC handler can create the
 * broadcast sink object.
 */
void bleBapBroadcastSinkSetTarget(uint32_t broadcast_id);

/** @brief Whether the broadcast sink stream is in the Streaming state. */
bool bleBapBroadcastSinkStreaming(void);

/** @brief Tear down the broadcast sink + scan delegator + gap observer. */
void bleBapBroadcastSinkDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
