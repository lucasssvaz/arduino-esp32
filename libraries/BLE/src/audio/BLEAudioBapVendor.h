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
 * @brief C language boundary for the ESP-BLE-AUDIO BAP (Basic Audio Profile) engine.
 *
 * The vendor BAP headers pull in the Zephyr bluetooth-audio headers and rely on
 * compound-literal / designated-initializer macros (`ESP_BLE_AUDIO_CODEC_CAP_LC3`,
 * `ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_*_DEFINE`, `ESP_BLE_AUDIO_BAP_QOS_CFG_PREF`)
 * that are GNU-C, not ISO C++. All of that -- the codec capability/preset tables,
 * the PACS/ASCS registration, the unicast server ASE-control callbacks, the
 * client discover->config->qos->enable->connect->start orchestration, and the
 * `esp_ble_audio_bap_stream_t` pools -- therefore lives in ONE C translation
 * unit (`BLEAudioBapVendor.c`). The C++ layer above it reaches this narrow,
 * C-safe surface only, referring to a stream by a small integer "slot" and a
 * direction. Mirrors the ESP-BLE-ISO boundary (`BLEAudioIsoVendor.h`); it is a
 * language boundary, not a per-backend shim.
 *
 * Roles wired here:
 *  - Unicast Server (BAP acceptor / peripheral: PACS + ASCS) -- accepts an
 *    inbound CIS, receives transparent SDUs on its sink ASE, and can stream
 *    transparent SDUs on its source ASE.
 *  - Unicast Client (BAP initiator / central) -- runs the full stream setup
 *    sequence over an established ACL and streams transparent SDUs on the sink
 *    ASE it configured on the peer.
 *
 * The boundary drives a single unicast link (one sink + one source stream),
 * which is spec-valid and matches the ESP-to-ESP unicast demo/validation. The
 * data plane is transparent SDUs (HCI ISO data path); LC3/I2S is layered on top
 * later (Phase 3) without touching this file.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Direction of a BAP stream, relative to the local device's ASE. */
#define BLE_BAP_VENDOR_DIR_SINK   0 /*!< Local sink ASE (receives SDUs). */
#define BLE_BAP_VENDOR_DIR_SOURCE 1 /*!< Local source ASE (transmits SDUs). */

/**
 * @brief Standard BAP LC3 preset selector.
 *
 * Maps to the vendor `ESP_BLE_AUDIO_BAP_LC3_*_PRESET_<rate>_<frame>_<rel>_DEFINE`
 * macros inside the C TU (kept there because those macros are GNU-C).
 */
typedef enum {
  BLE_BAP_VENDOR_PRESET_16_2_1 = 0, /*!< 16 kHz, 10 ms  (mandatory) */
  BLE_BAP_VENDOR_PRESET_24_2_1,     /*!< 24 kHz, 10 ms  (mandatory server) */
  BLE_BAP_VENDOR_PRESET_48_4_1,     /*!< 48 kHz, 10 ms  (high quality) */
} ble_bap_vendor_preset_t;

/**
 * @brief C-safe copy of a BAP SDU's receive metadata.
 *
 * The vendor `esp_ble_iso_recv_info_t` (a Zephyr struct) never crosses the
 * boundary; the C TU decodes its flag bitfield into plain booleans here.
 */
typedef struct {
  uint32_t timestamp_us; /*!< Controller timestamp; only meaningful if ts_valid. */
  uint16_t seq_num;      /*!< SDU sequence number. */
  bool valid;            /*!< Payload is complete and error-free. */
  bool ts_valid;         /*!< The timestamp field carries a valid value. */
} ble_bap_vendor_recv_info_t;

/** Stream lifecycle/data callbacks dispatched from the host task into C++. */
typedef void (*ble_bap_vendor_state_fn)(uint8_t dir);
typedef void (*ble_bap_vendor_reason_fn)(uint8_t dir, uint8_t reason);
typedef void (*ble_bap_vendor_recv_fn)(uint8_t dir, const ble_bap_vendor_recv_info_t *info, const uint8_t *data, uint16_t len);

typedef struct {
  ble_bap_vendor_state_fn started;  /*!< Stream entered the Streaming state. */
  ble_bap_vendor_reason_fn stopped; /*!< Stream left the Streaming state. */
  ble_bap_vendor_recv_fn recv;      /*!< A transparent SDU arrived (sink dir). */
  ble_bap_vendor_state_fn sent;     /*!< A queued SDU was sent (source dir). */
} ble_bap_vendor_stream_cbs_t;

/** @brief Register the C++ dispatch callbacks (call once before init). */
void bleBapVendorSetStreamCbs(const ble_bap_vendor_stream_cbs_t *cbs);

/* ── Unicast Server (acceptor / peripheral) ─────────────────────────────── */

/**
 * @brief Register PACS + ASCS with default LC3 capabilities and the given
 *        contexts/locations. Call after `esp_ble_audio_common_init` and before
 *        `esp_ble_audio_common_start` (i.e. between BLEAudio::begin and start).
 *
 * The default published capability advertises LC3 at any sampling frequency,
 * 10 ms frames, up to 2 channels, 40-120 octets/frame, 2 frames/SDU -- which
 * covers every mandatory unicast preset. On enable the engine auto-starts sink
 * ASEs (server responsibility), so received SDUs flow without app action.
 *
 * @param sink      Publish a sink PAC + sink ASE(s).
 * @param source    Publish a source PAC + source ASE(s).
 * @param sink_ctx  Available/supported sink context bitmask.
 * @param src_ctx   Available/supported source context bitmask.
 * @param sink_loc  Sink audio-location bitmask.
 * @param src_loc   Source audio-location bitmask.
 * @return 0 on success, or an error code.
 */
int bleBapVendorServerInit(bool sink, bool source, uint16_t sink_ctx, uint16_t src_ctx, uint32_t sink_loc, uint32_t src_loc);

/* ── Unicast Client (initiator / central) ───────────────────────────────── */

/** @brief Register the unicast client callbacks. Call between begin and start. */
int bleBapVendorClientInit(void);

/**
 * @brief Run the full unicast setup on an established+discovered ACL link.
 *
 * Kicks `discover(SINK)`; the internal state machine then chains
 * discover(SOURCE) -> config -> group create -> QoS -> enable -> CIS connect ->
 * start, exactly like the vendor unicast_client example, using @p preset for
 * both directions. Progress is reported through the stream callbacks.
 *
 * @param conn_handle ACL connection handle to the unicast server.
 * @param preset      LC3 preset to request.
 * @return 0 if discovery kicked off, or an error code.
 */
int bleBapVendorClientStart(uint16_t conn_handle, ble_bap_vendor_preset_t preset);

/** @brief Tear down the client's unicast group + stream state (on disconnect). */
void bleBapVendorClientReset(void);

/* ── Shared ─────────────────────────────────────────────────────────────── */

/**
 * @brief Send one transparent SDU on the local source stream.
 * @param dir     Must be BLE_BAP_VENDOR_DIR_SOURCE (sink is RX-only).
 * @param seq_num Monotonic per-stream SDU sequence number.
 * @return 0 on success, or an error code (e.g. not streaming / no credit).
 */
int bleBapVendorStreamSend(uint8_t dir, const uint8_t *sdu, uint16_t len, uint16_t seq_num);

/** @brief Whether the stream in the given direction is in the Streaming state. */
bool bleBapVendorStreamIsStreaming(uint8_t dir);

/** @brief Best-effort teardown of both roles' bookkeeping. */
void bleBapVendorDeinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
