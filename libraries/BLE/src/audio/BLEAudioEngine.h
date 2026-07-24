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
 * @brief Internal wrapper around the ESP-BLE-AUDIO engine lifecycle.
 *
 * This is the ONE place that owns the process-global engine lifecycle: the
 * single `esp_ble_audio_common_init` / `esp_ble_audio_common_start` and the
 * GAP/GATT callback bridge. The vendor headers do not parse as C++, so the
 * actual `esp_ble_audio_*` calls live in the sibling C translation unit
 * `BLEAudioVendor.c` and are reached through the C-safe `BLEAudioVendor.h`
 * surface; this layer only maps the result into `BTStatus`.
 *
 * This engine core is host-agnostic: it names no NimBLE/Bluedroid concept and
 * is a "fully shared" component (no per-backend `.nimble.*`/`.bluedroid.*`
 * files), matching the vendor API, which abstracts the host inside the engine.
 * The one place a host difference used to leak in -- the NimBLE GATT
 * coordinator's "audio mode" -- was inverted: the coordinator now observes this
 * engine's `isInitialized()` instead of being pushed from here. The header
 * therefore stays backend-agnostic so callers (BLEAudio, the front-door event
 * forwarders) don't drag vendor or stack headers in.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <cstdint>
#include "BTStatus.h"

namespace BLEAudioEngine {

/**
 * @brief Run the engine's common_init: GAP/GATT init + register the event bridge.
 *
 * The engine performs `svc_gap_init` / `svc_gatt_init` here and defers the
 * single `ble_gatts_start` to start(). On NimBLE the GATT coordinator observes
 * `isInitialized()` to decide it is in audio mode (so BLEServer only stages its
 * services); the engine does not reach into the coordinator itself.
 *
 * @return BTStatus::OK on success, or an error code.
 */
BTStatus init();

/**
 * @brief Run the engine's common_start: the single coordinated `ble_gatts_start`.
 *
 * Commits every staged service (audio profiles + any classic BLEServer
 * services) into one attribute table at once.
 *
 * @return BTStatus::OK on success, or an error code.
 */
BTStatus start();

/**
 * @brief Tear down the engine bookkeeping.
 * @note The prebuilt engine exposes no explicit common_deinit; this resets the
 *       wrapper's state so a later begin() is clean. On NimBLE the coordinator
 *       automatically leaves audio mode once `isInitialized()` reads false.
 */
void deinit();

/** @brief Whether init() has completed and deinit() has not been called. */
bool isInitialized();

/** @brief Whether start() has completed (GATT committed). */
bool isStarted();

/**
 * @brief Kick client-side GATT discovery of a peer's audio services on a connection.
 *
 * Host-agnostic: the underlying `esp_ble_audio_gattc_disc_start` exists on both
 * hosts and is idempotent (a duplicate kick is absorbed). It is *mandatory* to
 * call it explicitly on the NimBLE host and effectively automatic on Bluedroid,
 * so the library client backends simply call it once a connection is up. No-op
 * if the engine is not initialized.
 *
 * @param connHandle Connection handle of the freshly connected peer.
 */
void forwardGattcDiscStart(uint16_t connHandle);

}  // namespace BLEAudioEngine

#endif /* BLE_AUDIO_SUPPORTED */
