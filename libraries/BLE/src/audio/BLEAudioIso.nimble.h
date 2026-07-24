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
 * @file BLEAudioIso.nimble.h
 * @brief NimBLE-only glue between the host GAP event stream and the ISO engine.
 *
 * The shared transport core (`BLEAudioIso.{h,cpp}`) is host-agnostic and names
 * no NimBLE type. NimBLE, however, does not dispatch host GAP events into the
 * ISO engine automatically -- the BIS receiver needs the periodic-sync /
 * BIGInfo events forwarded via `esp_ble_iso_gap_app_post_event`. That
 * NimBLE-typed forwarding lives here (a `.nimble.*` file) so it never leaks into
 * the shared transport, and is called once from the scan/connection handlers.
 */

#include "core/BLEGuards.h"

#if BLE_NIMBLE && BLE_ISO_SUPPORTED

struct ble_gap_event;

namespace BLEAudioIso {

/**
 * @brief Forward a raw NimBLE GAP event into the ISO engine.
 *
 * Routes periodic-advertising sync + BIGInfo events into the engine so an armed
 * BIG sync (`syncBig`) fires when the BIGInfo report arrives on the train the
 * host synced to. No-op when the transport is inactive, so it is safe to call
 * unconditionally from the connection/scan handlers.
 *
 * @param event The NimBLE GAP event delivered to a scan/connection callback.
 */
void forwardHostGapEvent(struct ble_gap_event *event);

}  // namespace BLEAudioIso

#endif /* BLE_NIMBLE && BLE_ISO_SUPPORTED */
