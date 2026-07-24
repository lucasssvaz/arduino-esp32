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
 * @file BLEAudioEngine.nimble.h
 * @brief NimBLE-only glue between the host GAP/GATT event stream and the engine.
 *
 * The shared engine core (`BLEAudioEngine.{h,cpp}`) is host-agnostic and names
 * no NimBLE type. NimBLE, however, does not dispatch host GAP/GATT events into
 * the engine automatically -- the application must forward the raw
 * `struct ble_gap_event` via `esp_ble_audio_gap/gatt_app_post_event`. That
 * NimBLE-typed forwarding lives here (a `.nimble.*` file) so it never leaks
 * into the shared engine, and is called once from each NimBLE connection
 * handler (server + client) rather than duplicated in both TUs.
 */

#include "core/BLEGuards.h"

#if BLE_NIMBLE && BLE_AUDIO_SUPPORTED

struct ble_gap_event;

namespace BLEAudioEngine {

/**
 * @brief Forward a raw NimBLE GAP/GATT event into the LE Audio engine.
 *
 * Routes connection/security events through the engine's GAP path and
 * MTU/notify/subscribe events through its GATT path, and kicks client-side
 * discovery (`gattc_disc_start`) once the MTU is exchanged. No-op when the
 * engine is not initialized, so it is safe to call unconditionally from the
 * connection handlers.
 *
 * @param event The NimBLE GAP event delivered to a connection callback.
 */
void forwardHostGapEvent(struct ble_gap_event *event);

}  // namespace BLEAudioEngine

#endif /* BLE_NIMBLE && BLE_AUDIO_SUPPORTED */
