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
 * @file BLEAudioHearingAidVendor.c
 * @brief C language boundary for ESP-BLE-AUDIO Hearing Access Service calls.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioHearingAidVendor.h"

#include "esp_log.h"
#include <string.h>

__attribute__((unused)) static const char *HAS_TAG = "BLEAudioHAS";

/* ── Server (hearing aid device) ────────────────────────────────────────── */

#if BLE_AUDIO_HAS_SUPPORTED

#include "esp_ble_audio_has_api.h"

static ble_has_vendor_select_fn s_select_cb;

void bleHasVendorSetSelectCb(ble_has_vendor_select_fn fn) {
  s_select_cb = fn;
}

static int has_preset_select_cb(uint8_t index, bool sync) {
  if (s_select_cb) {
    s_select_cb(index, sync);
  }
  return 0;
}

static void has_preset_name_changed_cb(uint8_t index, const char *name) {
  ESP_LOGI(HAS_TAG, "preset %u renamed to %s", index, name ? name : "");
}

static const esp_ble_audio_has_preset_ops_t s_preset_ops = {
  .select = has_preset_select_cb,
  .name_changed = has_preset_name_changed_cb,
};

int bleHasVendorServerInit(uint8_t hearing_aid_type, bool preset_sync) {
  esp_ble_audio_has_features_param_t features = {0};
  features.type = (enum bt_has_hearing_aid_type)hearing_aid_type;
  features.preset_sync_support = preset_sync;

  esp_err_t err = esp_ble_audio_has_register(&features);
  if (err) {
    ESP_LOGE(HAS_TAG, "has_register: %d", err);
    return (int)err;
  }
  ESP_LOGI(HAS_TAG, "HAS registered (type=%u)", hearing_aid_type);
  return 0;
}

int bleHasVendorServerAddPreset(uint8_t index, bool writable, bool available, const char *name) {
  esp_ble_audio_has_preset_register_param_t param = {0};
  param.index = index;
  param.properties = (esp_ble_audio_has_properties_t)((writable ? BT_HAS_PROP_WRITABLE : 0) | (available ? BT_HAS_PROP_AVAILABLE : 0));
  param.name = name;
  param.ops = &s_preset_ops;

  esp_err_t err = esp_ble_audio_has_preset_register(&param);
  if (err) {
    ESP_LOGE(HAS_TAG, "preset_register(%u): %d", index, err);
    return (int)err;
  }
  return 0;
}

int bleHasVendorServerSetActive(uint8_t index) {
  return (int)esp_ble_audio_has_preset_active_set(index);
}

uint8_t bleHasVendorServerGetActive(void) {
  return esp_ble_audio_has_preset_active_get();
}

#else /* !BLE_AUDIO_HAS_SUPPORTED */

void bleHasVendorSetSelectCb(ble_has_vendor_select_fn fn) {
  (void)fn;
}
int bleHasVendorServerInit(uint8_t hearing_aid_type, bool preset_sync) {
  (void)hearing_aid_type;
  (void)preset_sync;
  return -1;
}
int bleHasVendorServerAddPreset(uint8_t index, bool writable, bool available, const char *name) {
  (void)index;
  (void)writable;
  (void)available;
  (void)name;
  return -1;
}
int bleHasVendorServerSetActive(uint8_t index) {
  (void)index;
  return -1;
}
uint8_t bleHasVendorServerGetActive(void) {
  return 0;
}

#endif /* BLE_AUDIO_HAS_SUPPORTED */

/* ── Client (hearing aid controller) ────────────────────────────────────── */

#if BLE_AUDIO_HAS_CLIENT_SUPPORTED

#include "esp_ble_audio_has_api.h"

static ble_has_vendor_client_cbs_t s_client_cbs;
static esp_ble_audio_has_t *s_has;  // cached from the discover callback

void bleHasVendorSetClientCbs(const ble_has_vendor_client_cbs_t *cbs) {
  if (cbs) {
    s_client_cbs = *cbs;
  } else {
    memset(&s_client_cbs, 0, sizeof(s_client_cbs));
  }
}

static void client_discover_cb(struct bt_conn *conn, int err, struct bt_has *has, enum bt_has_hearing_aid_type type, enum bt_has_capabilities caps) {
  (void)conn;
  s_has = has;
  if (s_client_cbs.discovered) {
    s_client_cbs.discovered(err, (uint8_t)type, (uint8_t)caps);
  }
}

static void client_preset_switch_cb(struct bt_has *has, int err, uint8_t index) {
  (void)has;
  if (s_client_cbs.preset_switch) {
    s_client_cbs.preset_switch(err, index);
  }
}

static void client_preset_read_rsp_cb(struct bt_has *has, int err, const struct bt_has_preset_record *record, bool is_last) {
  (void)has;
  if (s_client_cbs.preset_read) {
    uint8_t index = record ? record->index : 0;
    bool available = record ? ((record->properties & BT_HAS_PROP_AVAILABLE) != 0) : false;
    const char *name = record ? record->name : NULL;
    s_client_cbs.preset_read(err, index, available, name, is_last);
  }
}

static const esp_ble_audio_has_client_cb_t s_client_cb = {
  .discover = client_discover_cb,
  .preset_switch = client_preset_switch_cb,
  .preset_read_rsp = client_preset_read_rsp_cb,
};

int bleHasVendorClientInit(void) {
  esp_err_t err = esp_ble_audio_has_client_cb_register(&s_client_cb);
  if (err) {
    ESP_LOGE(HAS_TAG, "has_client_cb_register: %d", err);
    return (int)err;
  }
  return 0;
}

int bleHasVendorClientDiscover(uint16_t conn_handle) {
  s_has = NULL;
  return (int)esp_ble_audio_has_client_discover(conn_handle);
}

int bleHasVendorClientReadPresets(uint8_t start_index, uint8_t max_count) {
  if (!s_has) {
    return -1;
  }
  return (int)esp_ble_audio_has_client_presets_read(s_has, start_index, max_count);
}

int bleHasVendorClientSetPreset(uint8_t index, bool sync) {
  if (!s_has) {
    return -1;
  }
  return (int)esp_ble_audio_has_client_preset_set(s_has, index, sync);
}

int bleHasVendorClientNextPreset(bool sync) {
  if (!s_has) {
    return -1;
  }
  return (int)esp_ble_audio_has_client_preset_next(s_has, sync);
}

int bleHasVendorClientPrevPreset(bool sync) {
  if (!s_has) {
    return -1;
  }
  return (int)esp_ble_audio_has_client_preset_prev(s_has, sync);
}

#else /* !BLE_AUDIO_HAS_CLIENT_SUPPORTED */

void bleHasVendorSetClientCbs(const ble_has_vendor_client_cbs_t *cbs) {
  (void)cbs;
}
int bleHasVendorClientInit(void) {
  return -1;
}
int bleHasVendorClientDiscover(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}
int bleHasVendorClientReadPresets(uint8_t start_index, uint8_t max_count) {
  (void)start_index;
  (void)max_count;
  return -1;
}
int bleHasVendorClientSetPreset(uint8_t index, bool sync) {
  (void)index;
  (void)sync;
  return -1;
}
int bleHasVendorClientNextPreset(bool sync) {
  (void)sync;
  return -1;
}
int bleHasVendorClientPrevPreset(bool sync) {
  (void)sync;
  return -1;
}

#endif /* BLE_AUDIO_HAS_CLIENT_SUPPORTED */

void bleHasVendorDeinit(void) {
  bleHasVendorSetSelectCb(NULL);
  bleHasVendorSetClientCbs(NULL);
}

#endif /* BLE_AUDIO_SUPPORTED */
