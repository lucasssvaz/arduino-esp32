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
 * @brief C language boundary for the ESP-BLE-AUDIO top-level profile identity
 *        services: TMAP (Telephony & Media Audio Profile) and GMAP (Gaming
 *        Audio Profile).
 *
 * Both are thin "profile identity" layers on top of the CAP/BAP/control stack:
 * a server registers a TMAS instance advertising which top-level roles it
 * implements. GMAP server publish is stubbed on packaged libs (no GMAS). A
 * client discovers a peer's TMAS/GMAS to read the peer's roles. The actual
 * media/call/game audio still flows through CAP + BAP + the control profiles;
 * these services only advertise capability/identity.
 *
 * Role bitmasks mirror the engine's bit positions exactly, so values pass
 * through unmodified:
 *   TMAP: CG=BIT(0) CT=BIT(1) UMS=BIT(2) UMR=BIT(3) BMS=BIT(4) BMR=BIT(5)
 *   GMAP: UGG=BIT(0) UGT=BIT(1) BGS=BIT(2) BGR=BIT(3)
 *
 * All `esp_ble_audio_{tmap,gmap}_*` calls live in the sibling C translation
 * unit (`BLEAudioProfilesVendor.c`); the C++ layer reaches this surface only.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── TMAP ────────────────────────────────────────────────────────────────── */

/** Client: TMAP discovery completed; `peer_roles` is a TMAP role bitmask. */
typedef void (*ble_tmap_vendor_disc_fn)(int err, uint8_t peer_roles);

/** Install the TMAP discovery callback (set once before discover()). */
void bleTmapVendorSetDiscCb(ble_tmap_vendor_disc_fn fn);

/**
 * @brief Register a TMAS instance advertising @p roles. Call between
 *        BLEAudio::begin and start.
 * @return 0 on success, or an error code.
 */
int bleTmapVendorRegister(uint8_t roles);

/** @brief Discover a peer's TMAS over an ACL link (result via the disc cb). */
int bleTmapVendorDiscover(uint16_t conn_handle);

/* ── GMAP ────────────────────────────────────────────────────────────────── */

/** Client: GMAP discovery completed; `peer_roles` is a GMAP role bitmask. */
typedef void (*ble_gmap_vendor_disc_fn)(int err, uint8_t peer_roles);

/** Install the GMAP discovery callback (set once before discover()). */
void bleGmapVendorSetDiscCb(ble_gmap_vendor_disc_fn fn);

/**
 * @brief Register local GMAP roles/features (stubbed on packaged libs).
 *
 * Packaged `release/v6.1` libs have no host-adapter `gmas.c`: this returns
 * `-ENOTSUP` and does not call `esp_ble_audio_gmap_register`. Client discover
 * remains available. See `AUDIO.md`.
 * @return `-ENOTSUP` on packaged libs, or 0 / engine error once GMAS is shipped.
 */
int bleGmapVendorRegister(uint8_t roles, uint8_t ugg_feat, uint8_t ugt_feat, uint8_t bgs_feat, uint8_t bgr_feat);

/** @brief Discover a peer's GMAS over an ACL link (result via the disc cb). */
int bleGmapVendorDiscover(uint16_t conn_handle);

/* ── PBP (Public Broadcast Profile / Auracast announcement helper) ───────── */

/**
 * @brief Build a Public Broadcast Announcement (PBA) service-data payload.
 *
 * The output is the AD *value* for a 16-bit Service Data structure (assigned
 * number 0x16); prepend the length + AD type when assembling advertising data.
 * @param features PBP feature bits (0x01 encryption, 0x02 standard, 0x04 high).
 * @return number of bytes written to @p out, or a negative error code.
 */
int blePbpVendorBuildAnnouncement(const uint8_t *meta, uint16_t meta_len, uint8_t features, uint8_t *out, uint16_t out_cap);

/**
 * @brief Parse a received PBA service-data payload.
 * @param data      The AD value bytes (Service Data 16-bit).
 * @param data_len  Length of @p data.
 * @param features_out Parsed feature bits.
 * @param meta_out  Set to a pointer into @p data holding the metadata (or NULL).
 * @param meta_len_out Length of the metadata.
 * @return 0 on success, or a negative error code.
 */
int blePbpVendorParseAnnouncement(const uint8_t *data, uint8_t data_len, uint8_t *features_out, const uint8_t **meta_out, uint8_t *meta_len_out);

/** @brief Best-effort teardown of TMAP/GMAP dispatch bookkeeping. */
void bleProfilesVendorDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
