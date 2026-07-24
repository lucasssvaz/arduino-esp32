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
 * @file BLEAudioVendor.c
 * @brief C language boundary for ESP-BLE-AUDIO engine calls.
 *
 * Compiled as C so the vendor public header (and the host-internal headers it
 * pulls in, which rely on C-only opaque enum forward declarations) parse
 * correctly. This is the ONLY translation unit that names `esp_ble_audio_*`
 * types; everything above it goes through the C-safe BLEAudioVendor.h surface.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioVendor.h"
#include "audio/BLEAudioBapVendor.h"
#include "audio/BLEAudioCallVendor.h"
#include "audio/BLEAudioCapVendor.h"
#include "audio/BLEAudioCsipVendor.h"
#include "audio/BLEAudioHearingAidVendor.h"
#include "audio/BLEAudioIsoVendor.h"
#include "audio/BLEAudioMediaVendor.h"
#include "audio/BLEAudioMicVendor.h"
#include "audio/BLEAudioProfilesVendor.h"
#include "audio/BLEAudioVcpVendor.h"

#include "esp_ble_audio_common_api.h"
#include "esp_log.h"

/* Declared in the engine's common/host + app/{gap,gatt} headers (pulled
 * transitively by esp_ble_audio_common_api.h on the IDF include path). Named
 * explicitly so the teardown path does not depend on which transitive header
 * happens to expose them to this TU. */
void bt_le_host_deinit(void);
void bt_le_gap_app_cb_unregister(void);
void bt_le_gatt_app_cb_unregister(void);

static const char *BLE_AUDIO_VENDOR_TAG = "BLEAudioVendor";

/*
 * GAP/GATT event bridge. The engine hands back its host-abstracted app events
 * through C function pointers; keeping the callbacks here means the vendor event
 * structs never cross into C++. For the foundation they are observed and logged
 * so the wiring is exercised; Phase 1 routes them into the library dispatch.
 */
static void (*s_gap_observer)(uint8_t type, const void *event);

void bleAudioVendorSetGapObserver(void (*fn)(uint8_t type, const void *event)) {
  s_gap_observer = fn;
}

static void bleAudioVendorGapCb(struct bt_le_gap_app_event *event) {
  if (event == NULL) {
    return;
  }
  ESP_LOGV(BLE_AUDIO_VENDOR_TAG, "GAP app event type=%u", (unsigned)event->type);
  if (s_gap_observer) {
    s_gap_observer(event->type, event);
  }
}

static void bleAudioVendorGattCb(struct bt_le_gatt_app_event *event) {
  if (event == NULL) {
    return;
  }
  ESP_LOGV(BLE_AUDIO_VENDOR_TAG, "GATT app event type=%u", (unsigned)event->type);
}

int bleAudioVendorCommonInit(void) {
  esp_ble_audio_init_info_t info = {0};
  info.gap_cb = bleAudioVendorGapCb;
  info.gatt_cb = bleAudioVendorGattCb;
  return (int)esp_ble_audio_common_init(&info);
}

/*
 * Collect the CSIS service instances staged by the CAP-acceptor / CSIP
 * set-member roles.
 *
 * The engine creates a CSIS *object* at role registration but only adds it to
 * the GATT table from this start info, so an instance omitted here silently
 * never appears over the air. Worse, CAS is then committed with no included
 * service while the engine's profile library still expects the included-service
 * attribute, which skews the handle mapping of every service committed after
 * CAS (VCS, MICS, GMCS) and makes their attributes unreachable.
 */
static void bleAudioVendorFillCsisInsts(esp_ble_audio_start_info_t *info) {
#if CONFIG_BT_CSIP_SET_MEMBER
  /* CAS shall include no more than one CSIS instance, and the CAP acceptor
   * owns that one, so it is always claimed first. */
  void *insts[] = {
    bleCapVendorAcceptorCsisInst(),
    bleCsipVendorMemberCsisInst(),
  };
  const bool included_by_cas[] = {true, false};
  size_t next = 0;

  for (size_t i = 0; i < sizeof(insts) / sizeof(insts[0]); i++) {
    if (insts[i] == NULL) {
      continue;
    }
    if (next >= CONFIG_BT_CSIP_SET_MEMBER_MAX_INSTANCE_COUNT) {
      ESP_LOGE(BLE_AUDIO_VENDOR_TAG, "CSIS instances exceed BT_CSIP_SET_MEMBER_MAX_INSTANCE_COUNT (%d)", CONFIG_BT_CSIP_SET_MEMBER_MAX_INSTANCE_COUNT);
      break;
    }
    info->csis_insts[next].svc_inst = (esp_ble_audio_csip_set_member_svc_inst_t *)insts[i];
    info->csis_insts[next].included_by_cas = included_by_cas[i];
    next++;
  }
#else
  (void)info;
#endif /* CONFIG_BT_CSIP_SET_MEMBER */
}

int bleAudioVendorCommonStart(void) {
  esp_ble_audio_start_info_t info = {0};
  bleAudioVendorFillCsisInsts(&info);
  return (int)esp_ble_audio_common_start(&info);
}

void bleAudioVendorRolesDeinit(void) {
  /* Streams first: release ISO resources (BIG/CIG, ISO server) before the role
   * bookkeeping that owns them, so nothing outlives the controller. */
  bleBapVendorDeinit();
  bleIsoVendorDeinit();

  bleCapVendorDeinit();
  bleCsipVendorDeinit();
  bleVcpVendorDeinit();
  bleMicpVendorDeinit();
  bleMediaVendorDeinit();
  bleCallVendorDeinit();
  bleHasVendorDeinit();
  bleProfilesVendorDeinit();

  /* Mirror the failure-path cleanup inside esp_ble_audio_common_init: drop the
   * GAP/GATT app callbacks and tear down the engine host (ISO/scan + host_mutex).
   * There is still no profile-lib common_deinit (AICS/MCS/… keep AlreadyInit
   * statics), so a second common_init in the same boot typically still fails —
   * soft-reboot between audio sessions to clear those BSS pools. */
  bt_le_host_deinit();
  bt_le_gap_app_cb_unregister();
  bt_le_gatt_app_cb_unregister();
}

int bleAudioVendorGattcDiscStart(uint16_t conn_handle) {
  return (int)esp_ble_audio_gattc_disc_start(conn_handle);
}

void bleAudioVendorGapPostEvent(uint8_t type, void *event) {
  esp_ble_audio_gap_app_post_event(type, event);
}

#if BLE_NIMBLE
void bleAudioVendorGattPostEvent(uint8_t type, void *event) {
  esp_ble_audio_gatt_app_post_event(type, event);
}
#endif

#endif /* BLE_AUDIO_SUPPORTED */
