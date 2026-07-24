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
 * @file BLEAudioCapVendor.c
 * @brief C language boundary for ESP-BLE-AUDIO Common Audio Profile calls.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioCapVendor.h"

#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_cap_api.h"
#include "common/conn.h"
#include "esp_log.h"

#include <string.h>
#include <errno.h>

static const char *CAP_TAG = "BLEAudioCap";

/* ── Dispatch into C++ (set once before init) ───────────────────────────── */

static ble_cap_vendor_initiator_cbs_t s_init_cbs;
static ble_cap_vendor_commander_cbs_t s_cmd_cbs;

void bleCapVendorSetInitiatorCbs(const ble_cap_vendor_initiator_cbs_t *cbs) {
  if (cbs) {
    s_init_cbs = *cbs;
  } else {
    memset(&s_init_cbs, 0, sizeof(s_init_cbs));
  }
}

void bleCapVendorSetCommanderCbs(const ble_cap_vendor_commander_cbs_t *cbs) {
  if (cbs) {
    s_cmd_cbs = *cbs;
  } else {
    memset(&s_cmd_cbs, 0, sizeof(s_cmd_cbs));
  }
}

/* ── Acceptor (CAP server) ──────────────────────────────────────────────── */

#if BLE_AUDIO_CAP_ACCEPTOR_SUPPORTED

static esp_ble_audio_csip_set_member_svc_inst_t *s_acceptor_svc;

static void acceptor_lock_changed_cb(esp_ble_conn_t *conn, esp_ble_audio_csip_set_member_svc_inst_t *svc_inst, bool locked) {
  (void)conn;
  (void)svc_inst;
  ESP_LOGI(CAP_TAG, "acceptor set %s", locked ? "locked" : "released");
}

static uint8_t acceptor_sirk_read_req_cb(esp_ble_conn_t *conn, esp_ble_audio_csip_set_member_svc_inst_t *svc_inst) {
  (void)conn;
  (void)svc_inst;
  return ESP_BLE_AUDIO_CSIP_READ_SIRK_REQ_RSP_ACCEPT;
}

static esp_ble_audio_csip_set_member_cb_t s_acceptor_cb = {
  .lock_changed = acceptor_lock_changed_cb,
  .sirk_read_req = acceptor_sirk_read_req_cb,
};

int bleCapVendorAcceptorInit(const uint8_t sirk[BLE_CAP_VENDOR_SIRK_SIZE], uint8_t set_size, uint8_t rank, bool lockable) {
  esp_ble_audio_csip_set_member_register_param_t param = {0};
  param.set_size = set_size;
  memcpy(param.sirk, sirk, BLE_CAP_VENDOR_SIRK_SIZE);
  param.lockable = lockable;
  param.rank = rank;
  param.cb = &s_acceptor_cb;
  param.parent = NULL;

  esp_err_t err = esp_ble_audio_cap_acceptor_register(&param, &s_acceptor_svc);
  if (err) {
    ESP_LOGE(CAP_TAG, "cap_acceptor_register: %d", err);
    return (int)err;
  }
  ESP_LOGI(CAP_TAG, "acceptor registered (CAS + CSIS, size=%u rank=%u)", set_size, rank);
  return 0;
}

void *bleCapVendorAcceptorCsisInst(void) {
  return s_acceptor_svc;
}

#else /* !BLE_AUDIO_CAP_ACCEPTOR_SUPPORTED */

int bleCapVendorAcceptorInit(const uint8_t sirk[BLE_CAP_VENDOR_SIRK_SIZE], uint8_t set_size, uint8_t rank, bool lockable) {
  (void)sirk;
  (void)set_size;
  (void)rank;
  (void)lockable;
  return -1;
}

void *bleCapVendorAcceptorCsisInst(void) {
  return NULL;
}

#endif /* BLE_AUDIO_CAP_ACCEPTOR_SUPPORTED */

/* ── Initiator (CAP client) ─────────────────────────────────────────────── */

#if BLE_AUDIO_CAP_INITIATOR_SUPPORTED

static void initiator_disc_cb(esp_ble_conn_t *conn, int err, const esp_ble_audio_csip_set_coordinator_set_member_t *member, const esp_ble_audio_csip_set_coordinator_csis_inst_t *csis_inst) {
  (void)conn;
  (void)member;
  if (s_init_cbs.discovered) {
    s_init_cbs.discovered(err, csis_inst != NULL);
  }
}

static esp_ble_audio_cap_initiator_cb_t s_init_cb = {
  .unicast_discovery_complete = initiator_disc_cb,
};

int bleCapVendorInitiatorInit(void) {
  esp_err_t err = esp_ble_audio_cap_initiator_register_cb(&s_init_cb);
  if (err) {
    ESP_LOGE(CAP_TAG, "cap_initiator_register_cb: %d", err);
    return (int)err;
  }
  ESP_LOGI(CAP_TAG, "initiator registered");
  return 0;
}

int bleCapVendorInitiatorDiscover(uint16_t conn_handle) {
  esp_err_t err = esp_ble_audio_cap_initiator_unicast_discover(conn_handle);
  if (err) {
    ESP_LOGE(CAP_TAG, "cap_initiator_unicast_discover: %d", err);
  }
  return (int)err;
}

#else /* !BLE_AUDIO_CAP_INITIATOR_SUPPORTED */

int bleCapVendorInitiatorInit(void) {
  return -1;
}
int bleCapVendorInitiatorDiscover(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}

#endif /* BLE_AUDIO_CAP_INITIATOR_SUPPORTED */

/* ── Commander (CAP client) ─────────────────────────────────────────────── */

#if BLE_AUDIO_CAP_COMMANDER_SUPPORTED

static void commander_disc_cb(esp_ble_conn_t *conn, int err, const esp_ble_audio_csip_set_coordinator_set_member_t *member, const esp_ble_audio_csip_set_coordinator_csis_inst_t *csis_inst) {
  (void)conn;
  (void)member;
  (void)csis_inst;
  if (s_cmd_cbs.op_complete) {
    s_cmd_cbs.op_complete(BLE_CAP_VENDOR_OP_DISCOVER, err);
  }
}
static void commander_volume_cb(esp_ble_conn_t *conn, int err) {
  (void)conn;
  if (s_cmd_cbs.op_complete) {
    s_cmd_cbs.op_complete(BLE_CAP_VENDOR_OP_VOLUME, err);
  }
}
static void commander_volume_mute_cb(esp_ble_conn_t *conn, int err) {
  (void)conn;
  if (s_cmd_cbs.op_complete) {
    s_cmd_cbs.op_complete(BLE_CAP_VENDOR_OP_VOLUME_MUTE, err);
  }
}
static void commander_mic_mute_cb(esp_ble_conn_t *conn, int err) {
  (void)conn;
  if (s_cmd_cbs.op_complete) {
    s_cmd_cbs.op_complete(BLE_CAP_VENDOR_OP_MIC_MUTE, err);
  }
}

static esp_ble_audio_cap_commander_cb_t s_cmd_cb = {
  .discovery_complete = commander_disc_cb,
  .volume_changed = commander_volume_cb,
  .volume_mute_changed = commander_volume_mute_cb,
  .microphone_mute_changed = commander_mic_mute_cb,
};

int bleCapVendorCommanderInit(void) {
  esp_err_t err = esp_ble_audio_cap_commander_register_cb(&s_cmd_cb);
  if (err) {
    ESP_LOGE(CAP_TAG, "cap_commander_register_cb: %d", err);
    return (int)err;
  }
  ESP_LOGI(CAP_TAG, "commander registered");
  return 0;
}

int bleCapVendorCommanderDiscover(uint16_t conn_handle) {
  esp_err_t err = esp_ble_audio_cap_commander_discover(conn_handle);
  if (err) {
    ESP_LOGE(CAP_TAG, "cap_commander_discover: %d", err);
  }
  return (int)err;
}

/* Build a one-member ad-hoc set from a connection handle. */
static int fill_member(uint16_t conn_handle, esp_ble_audio_cap_set_member_t *member) {
  struct bt_conn *conn = bt_le_acl_conn_find_safe(conn_handle);
  if (!conn) {
    return -ENOTCONN;
  }
  member->member = conn;
  return 0;
}

int bleCapVendorCommanderChangeVolume(uint16_t conn_handle, uint8_t volume) {
  esp_ble_audio_cap_set_member_t member = {0};
  int rc = fill_member(conn_handle, &member);
  if (rc) {
    return rc;
  }
  esp_ble_audio_cap_commander_change_volume_param_t param = {0};
  param.type = ESP_BLE_AUDIO_CAP_SET_TYPE_AD_HOC;
  param.members = &member;
  param.count = 1;
  param.volume = volume;
  return (int)esp_ble_audio_cap_commander_change_volume(&param);
}

int bleCapVendorCommanderChangeVolumeMute(uint16_t conn_handle, bool mute) {
  esp_ble_audio_cap_set_member_t member = {0};
  int rc = fill_member(conn_handle, &member);
  if (rc) {
    return rc;
  }
  esp_ble_audio_cap_commander_change_volume_mute_state_param_t param = {0};
  param.type = ESP_BLE_AUDIO_CAP_SET_TYPE_AD_HOC;
  param.members = &member;
  param.count = 1;
  param.mute = mute;
  return (int)esp_ble_audio_cap_commander_change_volume_mute_state(&param);
}

int bleCapVendorCommanderChangeMicMute(uint16_t conn_handle, bool mute) {
  esp_ble_audio_cap_set_member_t member = {0};
  int rc = fill_member(conn_handle, &member);
  if (rc) {
    return rc;
  }
  esp_ble_audio_cap_commander_change_microphone_mute_state_param_t param = {0};
  param.type = ESP_BLE_AUDIO_CAP_SET_TYPE_AD_HOC;
  param.members = &member;
  param.count = 1;
  param.mute = mute;
  return (int)esp_ble_audio_cap_commander_change_microphone_mute_state(&param);
}

#else /* !BLE_AUDIO_CAP_COMMANDER_SUPPORTED */

int bleCapVendorCommanderInit(void) {
  return -1;
}
int bleCapVendorCommanderDiscover(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}
int bleCapVendorCommanderChangeVolume(uint16_t conn_handle, uint8_t volume) {
  (void)conn_handle;
  (void)volume;
  return -1;
}
int bleCapVendorCommanderChangeVolumeMute(uint16_t conn_handle, bool mute) {
  (void)conn_handle;
  (void)mute;
  return -1;
}
int bleCapVendorCommanderChangeMicMute(uint16_t conn_handle, bool mute) {
  (void)conn_handle;
  (void)mute;
  return -1;
}

#endif /* BLE_AUDIO_CAP_COMMANDER_SUPPORTED */

void bleCapVendorDeinit(void) {
  memset(&s_init_cbs, 0, sizeof(s_init_cbs));
  memset(&s_cmd_cbs, 0, sizeof(s_cmd_cbs));
#if BLE_AUDIO_CAP_ACCEPTOR_SUPPORTED
  s_acceptor_svc = NULL;
#endif
}

#endif /* BLE_AUDIO_SUPPORTED */
