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

/**
 * @file BLEAudioEngine.cpp
 * @brief ESP-BLE-AUDIO engine lifecycle + event-bridge implementation.
 *
 * Fully shared across NimBLE and Bluedroid: the ESP-BLE-AUDIO API is
 * host-agnostic (IDF abstracts the host inside the engine), so this core names
 * no NimBLE/Bluedroid concept and needs no backend guard. The vendor
 * `esp_ble_audio_*` headers do not parse as C++ (their host-internal includes
 * use C-only opaque enum forward declarations), so the actual engine calls live
 * in the C translation unit `BLEAudioVendor.c`; this file reaches them through
 * the C-safe `BLEAudioVendor.h` surface and maps the raw `esp_err_t` into the
 * library's `BTStatus`.
 *
 * The NimBLE GATT coordinator decides it is in "audio mode" by observing
 * `isInitialized()`; nothing here pushes into the coordinator, so this file
 * stays free of any stack concept.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioEngine.h"
#include "audio/BLEAudioVendor.h"
#include "esp32-hal-log.h"

namespace BLEAudioEngine {

namespace {
bool sInitialized = false;
bool sStarted = false;
}  // namespace

BTStatus init() {
  if (sInitialized) {
    return BTStatus::OK;
  }

  int err = bleAudioVendorCommonInit();
  if (err != 0) {
    log_e("esp_ble_audio_common_init failed: %d", err);
    return BTStatus::Fail;
  }

  sInitialized = true;

  // The engine has now run svc_gap_init/svc_gatt_init and staged the audio
  // profiles; deferring ble_gatts_start to start(). On NimBLE the GATT
  // coordinator observes isInitialized() (now true) to know the classic server
  // must only stage services -- no push from here.
  log_i("BLEAudioEngine: initialized");
  return BTStatus::OK;
}

BTStatus start() {
  if (!sInitialized) {
    log_e("BLEAudioEngine: start before init");
    return BTStatus::InvalidState;
  }
  if (sStarted) {
    return BTStatus::OK;
  }

  int err = bleAudioVendorCommonStart();
  if (err != 0) {
    log_e("esp_ble_audio_common_start failed: %d", err);
    return BTStatus::Fail;
  }

  sStarted = true;
  log_i("BLEAudioEngine: started (GATT committed)");
  return BTStatus::OK;
}

void deinit() {
  // Best-effort teardown: role bookkeeping + engine host_deinit + app-cb
  // unregister (same cleanup common_init uses on failure). The prebuilt
  // profile libs still have no common_deinit, so their AlreadyInit statics
  // survive — a second begin() in the same boot usually fails; soft-reboot
  // between audio sessions. On NimBLE the GATT coordinator leaves audio mode
  // once isInitialized() reads false again.
  bleAudioVendorRolesDeinit();
  sStarted = false;
  sInitialized = false;
}

bool isInitialized() {
  return sInitialized;
}

bool isStarted() {
  return sStarted;
}

void forwardGattcDiscStart(uint16_t connHandle) {
  if (!sInitialized) {
    return;
  }
  int err = bleAudioVendorGattcDiscStart(connHandle);
  if (err != 0) {
    log_w("esp_ble_audio_gattc_disc_start(%u) failed: %d", (unsigned)connHandle, err);
  }
}

}  // namespace BLEAudioEngine

#endif /* BLE_AUDIO_SUPPORTED */
