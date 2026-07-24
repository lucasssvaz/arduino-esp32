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
 * @file BLEAudioMediaVendor.c
 * @brief C language boundary for ESP-BLE-AUDIO Media Control Profile calls.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioMediaVendor.h"

#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_media_proxy_api.h"
#include "esp_ble_audio_mcc_api.h"
#include "esp_log.h"

#include <string.h>

static const char *MCP_TAG = "BLEAudioMedia";

/* ── Server (MCS) ───────────────────────────────────────────────────────── */

#if BLE_AUDIO_MCP_SERVER_SUPPORTED

int bleMediaVendorServerInit(void) {
  esp_err_t err = esp_ble_audio_media_proxy_pl_init();
  if (err) {
    ESP_LOGE(MCP_TAG, "media_proxy_pl_init: %d", err);
    return (int)err;
  }
  ESP_LOGI(MCP_TAG, "reference media player started (MCS)");
  return 0;
}

#else /* !BLE_AUDIO_MCP_SERVER_SUPPORTED */

int bleMediaVendorServerInit(void) {
  return -1;
}

#endif /* BLE_AUDIO_MCP_SERVER_SUPPORTED */

/* ── Client (MCC) ───────────────────────────────────────────────────────── */

#if BLE_AUDIO_MCP_CLIENT_SUPPORTED

static ble_media_vendor_client_cbs_t s_client_cbs;

void bleMediaVendorSetClientCbs(const ble_media_vendor_client_cbs_t *cbs) {
  if (cbs) {
    s_client_cbs = *cbs;
  } else {
    memset(&s_client_cbs, 0, sizeof(s_client_cbs));
  }
}

static void client_disc_cb(esp_ble_conn_t *conn, int err) {
  (void)conn;
  if (s_client_cbs.discovered) {
    s_client_cbs.discovered(err);
  }
}
static void client_state_cb(esp_ble_conn_t *conn, int err, uint8_t state) {
  (void)conn;
  if (s_client_cbs.state) {
    s_client_cbs.state(err, state);
  }
}
static void client_send_cmd_cb(esp_ble_conn_t *conn, int err, const esp_ble_audio_mpl_cmd_t *cmd) {
  (void)conn;
  if (s_client_cbs.cmd_sent) {
    s_client_cbs.cmd_sent(err, cmd ? cmd->opcode : 0);
  }
}
static void client_cmd_ntf_cb(esp_ble_conn_t *conn, int err, const esp_ble_audio_mpl_cmd_ntf_t *ntf) {
  (void)conn;
  if (s_client_cbs.cmd_ntf) {
    s_client_cbs.cmd_ntf(err, ntf ? ntf->requested_opcode : 0, ntf ? ntf->result_code : 0);
  }
}

static esp_ble_audio_mcc_cb_t s_client_cb = {
  .discover_mcs = client_disc_cb,
  .read_media_state = client_state_cb,
  .send_cmd = client_send_cmd_cb,
  .cmd_ntf = client_cmd_ntf_cb,
};

int bleMediaVendorClientInit(void) {
  esp_err_t err = esp_ble_audio_mcc_init(&s_client_cb);
  if (err) {
    ESP_LOGE(MCP_TAG, "mcc_init: %d", err);
    return (int)err;
  }
  ESP_LOGI(MCP_TAG, "media control client initialized");
  return 0;
}

int bleMediaVendorClientDiscover(uint16_t conn_handle, bool subscribe) {
  esp_err_t err = esp_ble_audio_mcc_discover_mcs(conn_handle, subscribe);
  if (err) {
    ESP_LOGE(MCP_TAG, "mcc_discover_mcs: %d", err);
  }
  return (int)err;
}

int bleMediaVendorClientReadState(uint16_t conn_handle) {
  return (int)esp_ble_audio_mcc_read_media_state(conn_handle);
}

int bleMediaVendorClientSendCommand(uint16_t conn_handle, uint8_t opcode, bool use_param, int32_t param) {
  esp_ble_audio_mpl_cmd_t cmd = {0};
  cmd.opcode = opcode;
  cmd.use_param = use_param;
  cmd.param = param;
  return (int)esp_ble_audio_mcc_send_cmd(conn_handle, &cmd);
}

void bleMediaVendorDeinit(void) {
  memset(&s_client_cbs, 0, sizeof(s_client_cbs));
}

#else /* !BLE_AUDIO_MCP_CLIENT_SUPPORTED */

void bleMediaVendorSetClientCbs(const ble_media_vendor_client_cbs_t *cbs) {
  (void)cbs;
}
int bleMediaVendorClientInit(void) {
  return -1;
}
int bleMediaVendorClientDiscover(uint16_t conn_handle, bool subscribe) {
  (void)conn_handle;
  (void)subscribe;
  return -1;
}
int bleMediaVendorClientReadState(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}
int bleMediaVendorClientSendCommand(uint16_t conn_handle, uint8_t opcode, bool use_param, int32_t param) {
  (void)conn_handle;
  (void)opcode;
  (void)use_param;
  (void)param;
  return -1;
}
void bleMediaVendorDeinit(void) {}

#endif /* BLE_AUDIO_MCP_CLIENT_SUPPORTED */

#endif /* BLE_AUDIO_SUPPORTED */
