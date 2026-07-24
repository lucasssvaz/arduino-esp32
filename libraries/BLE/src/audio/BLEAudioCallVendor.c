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
 * @file BLEAudioCallVendor.c
 * @brief C language boundary for ESP-BLE-AUDIO Call Control Profile (TBS/GTBS).
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioCallVendor.h"

#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_tbs_api.h"
#include "esp_log.h"

#include <string.h>

static const char *CCP_TAG = "BLEAudioCall";

/* ── Server (CCP / GTBS) ────────────────────────────────────────────────── */

#if BLE_AUDIO_CCP_SERVER_SUPPORTED

static ble_call_vendor_server_cbs_t s_server_cbs;

void bleCallVendorSetServerCbs(const ble_call_vendor_server_cbs_t *cbs) {
  if (cbs) {
    s_server_cbs = *cbs;
  } else {
    memset(&s_server_cbs, 0, sizeof(s_server_cbs));
  }
}

static bool server_originate_cb(esp_ble_conn_t *conn, uint8_t call_index, const char *uri) {
  (void)conn;
  if (s_server_cbs.originate) {
    return s_server_cbs.originate(call_index, uri);
  }
  return true;  // Accept by default so the demo call proceeds.
}

static void server_terminate_cb(esp_ble_conn_t *conn, uint8_t call_index, uint8_t reason) {
  (void)conn;
  if (s_server_cbs.terminated) {
    s_server_cbs.terminated(call_index, reason);
  }
}

static esp_ble_audio_tbs_cb_t s_server_cb = {
  .originate_call = server_originate_cb,
  .terminate_call = server_terminate_cb,
};

int bleCallVendorServerInit(const char *provider_name, const char *uci) {
  esp_ble_audio_tbs_register_cb(&s_server_cb);

  esp_ble_audio_tbs_register_param_t param = {0};
  param.provider_name = (char *)(provider_name ? provider_name : "ESP Phone");
  param.uci = (char *)(uci ? uci : "un000");
  param.uri_schemes_supported = (char *)"tel";
  param.gtbs = true;
  param.authorization_required = false;
  param.technology = ESP_BLE_AUDIO_TBS_TECHNOLOGY_LTE;
  param.supported_features = ESP_BLE_AUDIO_TBS_FEATURE_ALL;

  uint8_t bearer_index = 0;
  esp_err_t err = esp_ble_audio_tbs_register_bearer(&param, &bearer_index);
  if (err) {
    ESP_LOGE(CCP_TAG, "tbs_register_bearer: %d", err);
    return (int)err;
  }
  ESP_LOGI(CCP_TAG, "GTBS registered (index=%u)", bearer_index);
  return 0;
}

int bleCallVendorServerIncoming(const char *from, uint8_t *call_index) {
  return (int)esp_ble_audio_tbs_remote_incoming(ESP_BLE_AUDIO_TBS_GTBS_INDEX, "tel:server", from ? from : "tel:anonymous", from ? from : "Unknown", call_index);
}

int bleCallVendorServerTerminate(uint8_t call_index) {
  return (int)esp_ble_audio_tbs_terminate(call_index);
}

int bleCallVendorServerSetProviderName(const char *name) {
  return (int)esp_ble_audio_tbs_set_bearer_provider_name(ESP_BLE_AUDIO_TBS_GTBS_INDEX, name ? name : "ESP Phone");
}

#else /* !BLE_AUDIO_CCP_SERVER_SUPPORTED */

void bleCallVendorSetServerCbs(const ble_call_vendor_server_cbs_t *cbs) {
  (void)cbs;
}
int bleCallVendorServerInit(const char *provider_name, const char *uci) {
  (void)provider_name;
  (void)uci;
  return -1;
}
int bleCallVendorServerIncoming(const char *from, uint8_t *call_index) {
  (void)from;
  (void)call_index;
  return -1;
}
int bleCallVendorServerTerminate(uint8_t call_index) {
  (void)call_index;
  return -1;
}
int bleCallVendorServerSetProviderName(const char *name) {
  (void)name;
  return -1;
}

#endif /* BLE_AUDIO_CCP_SERVER_SUPPORTED */

/* ── Client (CCP / TBS client) ──────────────────────────────────────────── */

#if BLE_AUDIO_CCP_CLIENT_SUPPORTED

static ble_call_vendor_client_cbs_t s_client_cbs;

void bleCallVendorSetClientCbs(const ble_call_vendor_client_cbs_t *cbs) {
  if (cbs) {
    s_client_cbs = *cbs;
  } else {
    memset(&s_client_cbs, 0, sizeof(s_client_cbs));
  }
}

static void client_disc_cb(esp_ble_conn_t *conn, int err, uint8_t tbs_count, bool gtbs_found) {
  (void)conn;
  if (s_client_cbs.discovered) {
    s_client_cbs.discovered(err, tbs_count, gtbs_found);
  }
}

static void client_op_cb(ble_call_vendor_op_t op, esp_ble_conn_t *conn, int err, uint8_t inst_index, uint8_t call_index) {
  (void)conn;
  (void)inst_index;
  if (s_client_cbs.op_complete) {
    s_client_cbs.op_complete(op, err, call_index);
  }
}

static void client_originate_cb(esp_ble_conn_t *conn, int err, uint8_t inst_index, uint8_t call_index) {
  client_op_cb(BLE_CALL_VENDOR_OP_ORIGINATE, conn, err, inst_index, call_index);
}
static void client_accept_cb(esp_ble_conn_t *conn, int err, uint8_t inst_index, uint8_t call_index) {
  client_op_cb(BLE_CALL_VENDOR_OP_ACCEPT, conn, err, inst_index, call_index);
}
static void client_terminate_cb(esp_ble_conn_t *conn, int err, uint8_t inst_index, uint8_t call_index) {
  client_op_cb(BLE_CALL_VENDOR_OP_TERMINATE, conn, err, inst_index, call_index);
}
static void client_hold_cb(esp_ble_conn_t *conn, int err, uint8_t inst_index, uint8_t call_index) {
  client_op_cb(BLE_CALL_VENDOR_OP_HOLD, conn, err, inst_index, call_index);
}
static void client_retrieve_cb(esp_ble_conn_t *conn, int err, uint8_t inst_index, uint8_t call_index) {
  client_op_cb(BLE_CALL_VENDOR_OP_RETRIEVE, conn, err, inst_index, call_index);
}

static esp_ble_audio_tbs_client_cb_t s_client_cb = {
  .discover = client_disc_cb,
  .originate_call = client_originate_cb,
  .accept_call = client_accept_cb,
  .terminate_call = client_terminate_cb,
  .hold_call = client_hold_cb,
  .retrieve_call = client_retrieve_cb,
};

int bleCallVendorClientInit(void) {
  esp_err_t err = esp_ble_audio_tbs_client_register_cb(&s_client_cb);
  if (err) {
    ESP_LOGE(CCP_TAG, "tbs_client_register_cb: %d", err);
    return (int)err;
  }
  ESP_LOGI(CCP_TAG, "call control client initialized");
  return 0;
}

int bleCallVendorClientDiscover(uint16_t conn_handle) {
  esp_err_t err = esp_ble_audio_tbs_client_discover(conn_handle);
  if (err) {
    ESP_LOGE(CCP_TAG, "tbs_client_discover: %d", err);
  }
  return (int)err;
}

int bleCallVendorClientOriginate(uint16_t conn_handle, const char *uri) {
  return (int)esp_ble_audio_tbs_client_originate_call(conn_handle, ESP_BLE_AUDIO_TBS_GTBS_INDEX, uri ? uri : "tel:0");
}

int bleCallVendorClientAccept(uint16_t conn_handle, uint8_t call_index) {
  return (int)esp_ble_audio_tbs_client_accept_call(conn_handle, ESP_BLE_AUDIO_TBS_GTBS_INDEX, call_index);
}

int bleCallVendorClientTerminate(uint16_t conn_handle, uint8_t call_index) {
  return (int)esp_ble_audio_tbs_client_terminate_call(conn_handle, ESP_BLE_AUDIO_TBS_GTBS_INDEX, call_index);
}

#else /* !BLE_AUDIO_CCP_CLIENT_SUPPORTED */

void bleCallVendorSetClientCbs(const ble_call_vendor_client_cbs_t *cbs) {
  (void)cbs;
}
int bleCallVendorClientInit(void) {
  return -1;
}
int bleCallVendorClientDiscover(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}
int bleCallVendorClientOriginate(uint16_t conn_handle, const char *uri) {
  (void)conn_handle;
  (void)uri;
  return -1;
}
int bleCallVendorClientAccept(uint16_t conn_handle, uint8_t call_index) {
  (void)conn_handle;
  (void)call_index;
  return -1;
}
int bleCallVendorClientTerminate(uint16_t conn_handle, uint8_t call_index) {
  (void)conn_handle;
  (void)call_index;
  return -1;
}

#endif /* BLE_AUDIO_CCP_CLIENT_SUPPORTED */

void bleCallVendorDeinit(void) {
#if BLE_AUDIO_CCP_SERVER_SUPPORTED
  memset(&s_server_cbs, 0, sizeof(s_server_cbs));
#endif
#if BLE_AUDIO_CCP_CLIENT_SUPPORTED
  memset(&s_client_cbs, 0, sizeof(s_client_cbs));
#endif
}

#endif /* BLE_AUDIO_SUPPORTED */
