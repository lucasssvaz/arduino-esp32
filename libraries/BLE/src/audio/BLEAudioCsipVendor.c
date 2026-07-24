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
 * @file BLEAudioCsipVendor.c
 * @brief C language boundary for ESP-BLE-AUDIO Coordinated Set Identification calls.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioCsipVendor.h"

#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_csip_api.h"
#include "esp_log.h"

#include <string.h>
#include <errno.h>

static const char *CSIP_TAG = "BLEAudioCsip";

/* ── Dispatch into C++ (set once before init) ───────────────────────────── */

static ble_csip_vendor_member_cbs_t s_member_cbs;
static ble_csip_vendor_coord_cbs_t s_coord_cbs;

void bleCsipVendorSetMemberCbs(const ble_csip_vendor_member_cbs_t *cbs) {
  if (cbs) {
    s_member_cbs = *cbs;
  } else {
    memset(&s_member_cbs, 0, sizeof(s_member_cbs));
  }
}

void bleCsipVendorSetCoordinatorCbs(const ble_csip_vendor_coord_cbs_t *cbs) {
  if (cbs) {
    s_coord_cbs = *cbs;
  } else {
    memset(&s_coord_cbs, 0, sizeof(s_coord_cbs));
  }
}

/* ── Set Member (CSIP server) ───────────────────────────────────────────── */

#if BLE_AUDIO_CSIP_MEMBER_SUPPORTED

static esp_ble_audio_csip_set_member_svc_inst_t *s_member_svc;
static bool s_member_locked;

static void member_lock_changed_cb(esp_ble_conn_t *conn, esp_ble_audio_csip_set_member_svc_inst_t *svc_inst, bool locked) {
  (void)conn;
  (void)svc_inst;
  s_member_locked = locked;
  if (s_member_cbs.lock_changed) {
    s_member_cbs.lock_changed(locked);
  }
}

static uint8_t member_sirk_read_req_cb(esp_ble_conn_t *conn, esp_ble_audio_csip_set_member_svc_inst_t *svc_inst) {
  (void)conn;
  (void)svc_inst;
  return ESP_BLE_AUDIO_CSIP_READ_SIRK_REQ_RSP_ACCEPT;
}

static esp_ble_audio_csip_set_member_cb_t s_member_cb = {
  .lock_changed = member_lock_changed_cb,
  .sirk_read_req = member_sirk_read_req_cb,
};

int bleCsipVendorMemberInit(const uint8_t sirk[BLE_CSIP_VENDOR_SIRK_SIZE], uint8_t set_size, uint8_t rank, bool lockable) {
  esp_ble_audio_csip_set_member_register_param_t param = {0};
  param.set_size = set_size;
  memcpy(param.sirk, sirk, BLE_CSIP_VENDOR_SIRK_SIZE);
  param.lockable = lockable;
  param.rank = rank;
  param.cb = &s_member_cb;
  param.parent = NULL;

  esp_err_t err = esp_ble_audio_csip_set_member_register(&param, &s_member_svc);
  if (err) {
    ESP_LOGE(CSIP_TAG, "set_member_register: %d", err);
    return (int)err;
  }
  ESP_LOGI(CSIP_TAG, "set member registered (size=%u rank=%u lockable=%d)", set_size, rank, (int)lockable);
  return 0;
}

void *bleCsipVendorMemberCsisInst(void) {
  return s_member_svc;
}

int bleCsipVendorMemberSetSirk(const uint8_t sirk[BLE_CSIP_VENDOR_SIRK_SIZE]) {
  if (!s_member_svc) {
    return -EINVAL;
  }
  return (int)esp_ble_audio_csip_set_member_sirk(s_member_svc, sirk);
}
int bleCsipVendorMemberSetSizeAndRank(uint8_t set_size, uint8_t rank) {
  if (!s_member_svc) {
    return -EINVAL;
  }
  return (int)esp_ble_audio_csip_set_member_set_size_and_rank(s_member_svc, set_size, rank);
}
int bleCsipVendorMemberGenerateRsi(uint8_t rsi[BLE_CSIP_VENDOR_RSI_SIZE]) {
  if (!s_member_svc) {
    return -EINVAL;
  }
  return (int)esp_ble_audio_csip_set_member_generate_rsi(s_member_svc, rsi);
}
int bleCsipVendorMemberLock(bool lock, bool force) {
  if (!s_member_svc) {
    return -EINVAL;
  }
  return (int)esp_ble_audio_csip_set_member_lock(s_member_svc, lock, force);
}
bool bleCsipVendorMemberIsLocked(void) {
  return s_member_locked;
}

#else /* !BLE_AUDIO_CSIP_MEMBER_SUPPORTED */

int bleCsipVendorMemberInit(const uint8_t sirk[BLE_CSIP_VENDOR_SIRK_SIZE], uint8_t set_size, uint8_t rank, bool lockable) {
  (void)sirk;
  (void)set_size;
  (void)rank;
  (void)lockable;
  return -1;
}
void *bleCsipVendorMemberCsisInst(void) {
  return NULL;
}
int bleCsipVendorMemberSetSirk(const uint8_t sirk[BLE_CSIP_VENDOR_SIRK_SIZE]) {
  (void)sirk;
  return -1;
}
int bleCsipVendorMemberSetSizeAndRank(uint8_t set_size, uint8_t rank) {
  (void)set_size;
  (void)rank;
  return -1;
}
int bleCsipVendorMemberGenerateRsi(uint8_t rsi[BLE_CSIP_VENDOR_RSI_SIZE]) {
  (void)rsi;
  return -1;
}
int bleCsipVendorMemberLock(bool lock, bool force) {
  (void)lock;
  (void)force;
  return -1;
}
bool bleCsipVendorMemberIsLocked(void) {
  return false;
}

#endif /* BLE_AUDIO_CSIP_MEMBER_SUPPORTED */

/* ── Set Coordinator (CSIP client) ──────────────────────────────────────── */

#if BLE_AUDIO_CSIP_COORDINATOR_SUPPORTED

static void coord_discover_cb(esp_ble_conn_t *conn, const esp_ble_audio_csip_set_coordinator_set_member_t *member, int err, size_t set_count) {
  (void)conn;
  uint8_t set_size = 0;
  uint8_t rank = 0;
  if (!err && member && set_count > 0 && member->insts) {
    set_size = member->insts[0].info.set_size;
    rank = member->insts[0].info.rank;
  }
  if (s_coord_cbs.discovered) {
    s_coord_cbs.discovered(err, (uint8_t)set_count, set_size, rank);
  }
}

static esp_ble_audio_csip_set_coordinator_cb_t s_coord_cb = {
  .discover = coord_discover_cb,
};

int bleCsipVendorCoordinatorInit(void) {
  esp_err_t err = esp_ble_audio_csip_set_coordinator_register_cb(&s_coord_cb);
  if (err) {
    ESP_LOGE(CSIP_TAG, "set_coordinator_register_cb: %d", err);
    return (int)err;
  }
  ESP_LOGI(CSIP_TAG, "set coordinator registered");
  return 0;
}

int bleCsipVendorCoordinatorDiscover(uint16_t conn_handle) {
  esp_err_t err = esp_ble_audio_csip_set_coordinator_discover(conn_handle);
  if (err) {
    ESP_LOGE(CSIP_TAG, "set_coordinator_discover: %d", err);
  }
  return (int)err;
}

#else /* !BLE_AUDIO_CSIP_COORDINATOR_SUPPORTED */

int bleCsipVendorCoordinatorInit(void) {
  return -1;
}
int bleCsipVendorCoordinatorDiscover(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}

#endif /* BLE_AUDIO_CSIP_COORDINATOR_SUPPORTED */

void bleCsipVendorDeinit(void) {
  memset(&s_member_cbs, 0, sizeof(s_member_cbs));
  memset(&s_coord_cbs, 0, sizeof(s_coord_cbs));
#if BLE_AUDIO_CSIP_MEMBER_SUPPORTED
  s_member_svc = NULL;
  s_member_locked = false;
#endif
}

#endif /* BLE_AUDIO_SUPPORTED */
