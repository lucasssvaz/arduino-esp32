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
 * @file BLEAudioMicVendor.c
 * @brief C language boundary for ESP-BLE-AUDIO Microphone Control Profile calls.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioMicVendor.h"

#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_micp_api.h"
#include "esp_ble_audio_aics_api.h"
#include "esp_log.h"

#include <string.h>
#include <errno.h>

static const char *MICP_TAG = "BLEAudioMicp";

#ifndef CONFIG_BT_MICP_MIC_DEV_AICS_INSTANCE_COUNT
#define CONFIG_BT_MICP_MIC_DEV_AICS_INSTANCE_COUNT 0
#endif
#define MICP_AICS_INST_COUNT CONFIG_BT_MICP_MIC_DEV_AICS_INSTANCE_COUNT

/* ── Dispatch into C++ (set once before init) ───────────────────────────── */

static ble_micp_vendor_dev_cbs_t s_dev_cbs;
static ble_micp_vendor_ctlr_cbs_t s_ctlr_cbs;

void bleMicpVendorSetDeviceCbs(const ble_micp_vendor_dev_cbs_t *cbs) {
  if (cbs) {
    s_dev_cbs = *cbs;
  } else {
    memset(&s_dev_cbs, 0, sizeof(s_dev_cbs));
  }
}

void bleMicpVendorSetControllerCbs(const ble_micp_vendor_ctlr_cbs_t *cbs) {
  if (cbs) {
    s_ctlr_cbs = *cbs;
  } else {
    memset(&s_ctlr_cbs, 0, sizeof(s_ctlr_cbs));
  }
}

/* ── Microphone Device (MICP server) ────────────────────────────────────── */

#if BLE_AUDIO_MICP_DEVICE_SUPPORTED

static uint8_t s_dev_mute;

static void dev_mute_cb(uint8_t mute) {
  s_dev_mute = mute;
  if (s_dev_cbs.mute) {
    s_dev_cbs.mute(mute);
  }
}

static esp_ble_audio_micp_mic_dev_cb_t s_dev_cb = {
  .mute = dev_mute_cb,
};

#if MICP_AICS_INST_COUNT > 0
static void dev_aics_state_cb(esp_ble_audio_aics_t *inst, int err, int8_t gain, uint8_t mute, uint8_t mode) {
  (void)inst;
  (void)err;
  (void)gain;
  (void)mute;
  (void)mode;
}
static esp_ble_audio_aics_cb_t s_dev_aics_cb = {.state = dev_aics_state_cb};
static char s_dev_aics_desc[MICP_AICS_INST_COUNT][16];
#endif

int bleMicpVendorDeviceInit(uint8_t mute) {
  esp_ble_audio_micp_mic_dev_register_param_t param = {0};

#if MICP_AICS_INST_COUNT > 0
  static esp_ble_audio_aics_register_param_t aics_param[MICP_AICS_INST_COUNT];
  memset(aics_param, 0, sizeof(aics_param));
  for (size_t i = 0; i < MICP_AICS_INST_COUNT; i++) {
    aics_param[i].gain_mode = ESP_BLE_AUDIO_AICS_MODE_MANUAL;
    aics_param[i].units = 1;
    aics_param[i].min_gain = -100;
    aics_param[i].max_gain = 100;
    aics_param[i].type = ESP_BLE_AUDIO_AICS_INPUT_TYPE_MICROPHONE;
    aics_param[i].status = true;
    aics_param[i].desc_writable = true;
    snprintf(s_dev_aics_desc[i], sizeof(s_dev_aics_desc[i]), "Mic %u", (unsigned)(i + 1));
    aics_param[i].description = s_dev_aics_desc[i];
    aics_param[i].cb = &s_dev_aics_cb;
  }
  param.aics_param = aics_param;
#endif

  param.cb = &s_dev_cb;
  s_dev_mute = mute;

  esp_err_t err = esp_ble_audio_micp_mic_dev_register(&param);
  if (err) {
    ESP_LOGE(MICP_TAG, "mic_dev_register: %d", err);
    return (int)err;
  }
  if (mute) {
    esp_ble_audio_micp_mic_dev_mute();
  }
  ESP_LOGI(MICP_TAG, "microphone device registered (mute=%u aics=%d)", mute, MICP_AICS_INST_COUNT);
  return 0;
}

int bleMicpVendorDeviceSetMute(bool mute) {
  return (int)(mute ? esp_ble_audio_micp_mic_dev_mute() : esp_ble_audio_micp_mic_dev_unmute());
}
int bleMicpVendorDeviceMuteDisable(void) {
  return (int)esp_ble_audio_micp_mic_dev_mute_disable();
}
bool bleMicpVendorDeviceIsMuted(void) {
  return s_dev_mute == ESP_BLE_AUDIO_MICP_MUTE_MUTED;
}

#else /* !BLE_AUDIO_MICP_DEVICE_SUPPORTED */

int bleMicpVendorDeviceInit(uint8_t mute) {
  (void)mute;
  return -1;
}
int bleMicpVendorDeviceSetMute(bool mute) {
  (void)mute;
  return -1;
}
int bleMicpVendorDeviceMuteDisable(void) {
  return -1;
}
bool bleMicpVendorDeviceIsMuted(void) {
  return false;
}

#endif /* BLE_AUDIO_MICP_DEVICE_SUPPORTED */

/* ── Microphone Controller (MICP client) ────────────────────────────────── */

#if BLE_AUDIO_MICP_CONTROLLER_SUPPORTED

static void ctlr_mute_cb(esp_ble_audio_micp_mic_ctlr_t *mic_ctlr, int err, uint8_t mute) {
  (void)mic_ctlr;
  if (s_ctlr_cbs.mute) {
    s_ctlr_cbs.mute(err, mute);
  }
}

static void ctlr_discover_cb(esp_ble_audio_micp_mic_ctlr_t *mic_ctlr, int err, uint8_t aics_count) {
  (void)mic_ctlr;
  if (s_ctlr_cbs.discovered) {
    s_ctlr_cbs.discovered(err, aics_count);
  }
}

static void ctlr_mute_written_cb(esp_ble_audio_micp_mic_ctlr_t *mic_ctlr, int err) {
  (void)mic_ctlr;
  (void)err;
}

static esp_ble_audio_micp_mic_ctlr_cb_t s_ctlr_cb = {
  .mute = ctlr_mute_cb,
  .discover = ctlr_discover_cb,
  .mute_written = ctlr_mute_written_cb,
};

int bleMicpVendorControllerInit(void) {
  esp_err_t err = esp_ble_audio_micp_mic_ctlr_cb_register(&s_ctlr_cb);
  if (err) {
    ESP_LOGE(MICP_TAG, "mic_ctlr_cb_register: %d", err);
    return (int)err;
  }
  ESP_LOGI(MICP_TAG, "microphone controller registered");
  return 0;
}

int bleMicpVendorControllerDiscover(uint16_t conn_handle) {
  esp_ble_audio_micp_mic_ctlr_t *mic_ctlr = NULL;
  esp_err_t err = esp_ble_audio_micp_mic_ctlr_discover(conn_handle, &mic_ctlr);
  if (err) {
    ESP_LOGE(MICP_TAG, "mic_ctlr_discover: %d", err);
  }
  return (int)err;
}

static esp_ble_audio_micp_mic_ctlr_t *ctlr_for(uint16_t conn_handle) {
  return esp_ble_audio_micp_mic_ctlr_get_by_conn(conn_handle);
}

int bleMicpVendorControllerSetMute(uint16_t conn_handle, bool mute) {
  esp_ble_audio_micp_mic_ctlr_t *c = ctlr_for(conn_handle);
  if (!c) {
    return -ENOTCONN;
  }
  return (int)(mute ? esp_ble_audio_micp_mic_ctlr_mute(c) : esp_ble_audio_micp_mic_ctlr_unmute(c));
}
int bleMicpVendorControllerReadMute(uint16_t conn_handle) {
  esp_ble_audio_micp_mic_ctlr_t *c = ctlr_for(conn_handle);
  if (!c) {
    return -ENOTCONN;
  }
  return (int)esp_ble_audio_micp_mic_ctlr_mute_get(c);
}

#else /* !BLE_AUDIO_MICP_CONTROLLER_SUPPORTED */

int bleMicpVendorControllerInit(void) {
  return -1;
}
int bleMicpVendorControllerDiscover(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}
int bleMicpVendorControllerSetMute(uint16_t conn_handle, bool mute) {
  (void)conn_handle;
  (void)mute;
  return -1;
}
int bleMicpVendorControllerReadMute(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}

#endif /* BLE_AUDIO_MICP_CONTROLLER_SUPPORTED */

void bleMicpVendorDeinit(void) {
  memset(&s_dev_cbs, 0, sizeof(s_dev_cbs));
  memset(&s_ctlr_cbs, 0, sizeof(s_ctlr_cbs));
}

#endif /* BLE_AUDIO_SUPPORTED */
