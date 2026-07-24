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
 * @brief C language boundary for the ESP-BLE-AUDIO engine calls.
 *
 * The vendor public header `esp_ble_audio_common_api.h` transitively pulls in
 * host-internal headers (`common/init.h` -> `lib/include/audio.h`) that use
 * C-only opaque enum forward declarations (`enum bt_bap_ascs_reason;`). Those
 * are ill-formed in C++, so the engine calls are compiled in ONE C translation
 * unit (`BLEAudioVendor.c`) and reached from C++ through this narrow, C-safe
 * surface. This is a language boundary, not a per-backend shim: it exists only
 * because the vendor headers do not parse as C++, and it names none of the
 * library's own abstractions.
 *
 * The functions return the raw `esp_err_t` value as `int` (0 == ESP_OK) so the
 * header stays free of any vendor typedef.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief `esp_ble_audio_common_init` with the engine event bridge installed. */
int bleAudioVendorCommonInit(void);

/**
 * @brief Observe engine GAP application events (translated `bt_le_gap_app_event`).
 *
 * The engine's `gap_cb` forwards every app event it raises to this optional
 * observer as an opaque pointer + type. Used by the broadcast sink (in the
 * broadcast vendor C TU) to react to PA_SYNC / PA_SYNC_LOST; the observer casts
 * @p event back to `struct bt_le_gap_app_event`. Pass NULL to clear.
 *
 * @param fn Observer callback, or NULL.
 */
void bleAudioVendorSetGapObserver(void (*fn)(uint8_t type, const void *event));

/** @brief `esp_ble_audio_common_start` (the single coordinated GATT commit). */
int bleAudioVendorCommonStart(void);

/**
 * @brief Best-effort teardown of every role vendor's bookkeeping + engine host.
 *
 * Releases ISO/role bookkeeping, then calls `bt_le_host_deinit` and unregisters
 * the GAP/GATT app callbacks (the same cleanup `esp_ble_audio_common_init` uses
 * on failure). The prebuilt profile libs still expose no full `common_deinit`,
 * so AICS/MCS/… `AlreadyInit` statics survive across `end()`/`begin()` in the
 * same boot — soft-reboot between audio sessions when a clean engine is
 * required.
 */
void bleAudioVendorRolesDeinit(void);

/**
 * @brief `esp_ble_audio_gattc_disc_start` (client-side discovery kick).
 *
 * Host-agnostic: the vendor symbol is declared unconditionally and used on both
 * hosts (mandatory-explicit on NimBLE, effectively automatic on Bluedroid), so
 * this boundary is not stack-guarded.
 */
int bleAudioVendorGattcDiscStart(uint16_t conn_handle);

/**
 * @brief `esp_ble_audio_gap_app_post_event` (forward a host GAP event into the engine).
 *
 * Host-agnostic boundary (the vendor symbol is declared for both hosts). The
 * forwarding is only wired on NimBLE, where the host does not dispatch app
 * events into the engine automatically; @p event is the opaque host event.
 */
void bleAudioVendorGapPostEvent(uint8_t type, void *event);

#if BLE_NIMBLE
/**
 * @brief `esp_ble_audio_gatt_app_post_event` (forward a host GATT event into the engine).
 *
 * NimBLE-only by nature: the vendor symbol is hidden on Bluedroid (which
 * dispatches GATT events directly inside its BTA adapter), so this boundary is
 * compiled only for NimBLE -- the guard mirrors the vendor's own
 * `#if !CONFIG_BT_BLUEDROID_ENABLED`, it is not a spurious stack dependency.
 */
void bleAudioVendorGattPostEvent(uint8_t type, void *event);
#endif

#ifdef __cplusplus
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
