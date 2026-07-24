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
 * @brief C language boundary for the ESP-BLE-ISO transport engine.
 *
 * The vendor public header `esp_ble_iso_common_api.h` pulls in the Zephyr
 * bluetooth headers (`zephyr/bluetooth/iso.h`, ...) whose opaque forward
 * declarations and macros do not parse as C++. The isochronous transport is
 * therefore driven from ONE C translation unit (`BLEAudioIsoVendor.c`) that
 * owns every `esp_ble_iso_*` struct (channels, QoS, CIG/BIG params, the ISO
 * server) and reaches C++ only through this narrow, C-safe surface. Mirrors
 * the ESP-BLE-AUDIO boundary (`BLEAudioVendor.h`); it is a language boundary,
 * not a per-backend shim, and names none of the library's own abstractions.
 *
 * The surface is deliberately audio-agnostic: it deals in ISO primitives
 * (channels, groups, transparent SDUs), so the C++ layer above it
 * (`audio/BLEAudioIso.{h,cpp}`) can later be promoted to a public BLE ISO
 * feature without touching this file.
 *
 * The C++ side refers to a channel by a small integer "slot" index into a
 * fixed pool the C TU owns; all vendor pointers stay behind the boundary.
 * Functions return the raw `esp_err_t` as `int` (0 == ESP_OK).
 */

#include "core/BLEGuards.h"
#if BLE_ISO_SUPPORTED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of concurrent ISO channels the transport pool holds. */
#define BLE_ISO_VENDOR_MAX_CHAN 4

/** PHY selectors (host-agnostic; mapped to the vendor consts inside the C TU). */
#define BLE_ISO_VENDOR_PHY_1M 1
#define BLE_ISO_VENDOR_PHY_2M 2
#define BLE_ISO_VENDOR_PHY_CODED 3

/** Broadcast code length (session key derivation input). */
#define BLE_ISO_VENDOR_BCODE_SIZE 16

/**
 * @brief C-safe copy of an ISO SDU's receive metadata.
 *
 * The vendor `esp_ble_iso_recv_info_t` (a Zephyr struct) never crosses the
 * boundary; the C TU decodes its flag bitfield into plain booleans here.
 */
typedef struct {
  uint32_t timestamp_us; /*!< Controller timestamp; only meaningful if ts_valid. */
  uint16_t seq_num;      /*!< SDU sequence number of the first fragment. */
  bool valid;            /*!< Payload is complete and error-free. */
  bool ts_valid;         /*!< The timestamp field carries a valid value. */
} ble_iso_vendor_recv_info_t;

/** Channel event callbacks dispatched from the host context into C++. */
typedef void (*ble_iso_vendor_connected_fn)(int slot);
typedef void (*ble_iso_vendor_disconnected_fn)(int slot, uint8_t reason);
typedef void (*ble_iso_vendor_recv_fn)(int slot, const ble_iso_vendor_recv_info_t *info, const uint8_t *data, uint16_t len);
typedef void (*ble_iso_vendor_sent_fn)(int slot);

/**
 * @brief Register the C++ dispatch callbacks (called once before begin()).
 *
 * All four fire from the ISO host task. The C TU auto-sets up the HCI data
 * path (transparent coding) in the connected path before invoking @p connected,
 * matching every esp_ble_iso example, so C++ only observes library-level events.
 */
void bleIsoVendorSetCallbacks(ble_iso_vendor_connected_fn connected, ble_iso_vendor_disconnected_fn disconnected, ble_iso_vendor_recv_fn recv, ble_iso_vendor_sent_fn sent);

/** @brief `esp_ble_iso_common_init` with the internal gap_cb installed. */
int bleIsoVendorInit(void);

/**
 * @brief Release the transport bookkeeping.
 *
 * The prebuilt engine exposes no common_deinit; this frees every slot and
 * clears the armed BIG-sync state so a later begin() starts clean.
 */
void bleIsoVendorDeinit(void);

/** @brief Whether bleIsoVendorInit() has completed and Deinit() has not run. */
bool bleIsoVendorIsActive(void);

/**
 * @brief Reserve a channel slot from the fixed pool.
 * @return Slot index in [0, BLE_ISO_VENDOR_MAX_CHAN), or -1 if the pool is full.
 */
int bleIsoVendorAllocSlot(void);

/** @brief Return a slot to the pool. */
void bleIsoVendorReleaseSlot(int slot);

/** @brief Whether the channel in @p slot is currently connected. */
bool bleIsoVendorIsConnected(int slot);

/**
 * @brief Create a CIG and connect a single CIS as Central on an ACL link.
 *
 * @param slot         Pre-allocated TX channel slot.
 * @param conn_handle  ACL connection handle owning the CIS.
 * @param sdu          Max SDU size in octets.
 * @param phy          One of BLE_ISO_VENDOR_PHY_*.
 * @param rtn          Retransmission number.
 * @param interval_us  SDU interval in microseconds (both directions).
 * @param latency_ms   Transport latency in milliseconds (both directions).
 * @param packing      0 sequential, 1 interleaved.
 * @param framing      0 unframed, 1 framed.
 * @return 0 on success (connection completes asynchronously via the connected
 *         callback), or an error code.
 */
int bleIsoVendorCisConnect(
  int slot, uint16_t conn_handle, uint16_t sdu, uint8_t phy, uint8_t rtn, uint32_t interval_us, uint16_t latency_ms, uint8_t packing, uint8_t framing
);

/**
 * @brief Register the ISO server so an incoming CIS is accepted on @p slot.
 *
 * @param slot  Pre-allocated RX channel slot handed to the accept callback.
 * @param sdu   Expected RX SDU size in octets.
 * @return 0 on success, or an error code.
 */
int bleIsoVendorCisListen(int slot, uint16_t sdu);

/**
 * @brief Create a BIG (broadcaster) bound to an existing ext-adv instance.
 *
 * The advertising set (extended + periodic) must already be running on
 * @p adv_handle; this adds it to the BIG and creates the group.
 *
 * @param slot         Pre-allocated TX channel slot (single BIS).
 * @param adv_handle   Advertising instance handle carrying the periodic train.
 * @param sdu,phy,rtn,interval_us,latency_ms,packing,framing  BIG QoS.
 * @param bcode        Optional broadcast code (encryption); NULL disables it.
 * @param bcode_len    Length of @p bcode (<= BLE_ISO_VENDOR_BCODE_SIZE).
 * @return 0 on success, or an error code.
 */
int bleIsoVendorBigCreate(
  int slot, uint8_t adv_handle, uint16_t sdu, uint8_t phy, uint8_t rtn, uint32_t interval_us, uint16_t latency_ms, uint8_t packing, uint8_t framing,
  const uint8_t *bcode, uint8_t bcode_len
);

/**
 * @brief Arm a BIG sync (receiver) for one BIS index.
 *
 * The actual `esp_ble_iso_big_sync` is issued from the internal gap_cb the
 * moment a BIGInfo report arrives for the periodic train the host is synced to
 * (the host must forward its periodic-sync GAP events via
 * bleIsoVendorGapPostEvent on NimBLE). Idempotent while armed.
 *
 * @param slot          Pre-allocated RX channel slot.
 * @param bis_index     1-based BIS index to receive.
 * @param sdu           Expected RX SDU size in octets.
 * @param sync_timeout  BIG sync timeout (N * 10 ms).
 * @param bcode         Optional broadcast code; NULL if the BIG is unencrypted.
 * @param bcode_len     Length of @p bcode.
 * @return 0 on success, or an error code.
 */
int bleIsoVendorBigSyncArm(int slot, uint8_t bis_index, uint16_t sdu, uint16_t sync_timeout, const uint8_t *bcode, uint8_t bcode_len);

/**
 * @brief Send one transparent SDU on a connected channel.
 * @return 0 on success, or an error code (e.g. no credits / not connected).
 */
int bleIsoVendorSend(int slot, const uint8_t *sdu, uint16_t len, uint16_t seq_num);

/**
 * @brief Forward a raw host GAP event into the ISO engine (NimBLE only).
 *
 * NimBLE does not dispatch app GAP events into the engine automatically; the
 * BIG receiver path relies on periodic-sync / BIGInfo events reaching the
 * engine so it can emit the BIGInfo report the arm logic waits on. No-op on
 * the CIS / broadcaster paths, so it is safe to call unconditionally.
 */
void bleIsoVendorGapPostEvent(uint8_t type, void *event);

#ifdef __cplusplus
}
#endif

#endif /* BLE_ISO_SUPPORTED */
