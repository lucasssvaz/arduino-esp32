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
 * @file BLEAudioVcpVendor.c
 * @brief C language boundary for ESP-BLE-AUDIO Volume Control Profile calls.
 *
 * Compiled as C so the vendor headers (and the Zephyr VCP/VOCS/AICS callback
 * structs they use) parse correctly. This is the ONLY translation unit that
 * names `esp_ble_audio_vcp_*` / `esp_ble_audio_vocs_*` / `esp_ble_audio_aics_*`
 * types; everything above it goes through the C-safe `BLEAudioVcpVendor.h`
 * surface. It condenses the vendor VCP renderer/controller setup to a single
 * renderer and a single controller link.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioVcpVendor.h"

#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_vcp_api.h"
#include "esp_ble_audio_vocs_api.h"
#include "esp_ble_audio_aics_api.h"
#include "esp_log.h"

#include <string.h>
#include <errno.h>

static const char *VCP_TAG = "BLEAudioVcp";

/* VOCS/AICS instance counts included under the renderer's VCS. Fall back to 0
 * so the file still builds if a config enables the renderer without the
 * per-instance count symbols. */
#ifndef CONFIG_BT_VCP_VOL_REND_VOCS_INSTANCE_COUNT
#define CONFIG_BT_VCP_VOL_REND_VOCS_INSTANCE_COUNT 0
#endif
#ifndef CONFIG_BT_VCP_VOL_REND_AICS_INSTANCE_COUNT
#define CONFIG_BT_VCP_VOL_REND_AICS_INSTANCE_COUNT 0
#endif
#define VCP_VOCS_INST_COUNT CONFIG_BT_VCP_VOL_REND_VOCS_INSTANCE_COUNT
#define VCP_AICS_INST_COUNT CONFIG_BT_VCP_VOL_REND_AICS_INSTANCE_COUNT

/* ── Dispatch into C++ (set once before init) ───────────────────────────── */

static ble_vcp_vendor_rend_cbs_t s_rend_cbs;
static ble_vcp_vendor_ctlr_cbs_t s_ctlr_cbs;

void bleVcpVendorSetRendererCbs(const ble_vcp_vendor_rend_cbs_t *cbs) {
  if (cbs) {
    s_rend_cbs = *cbs;
  } else {
    memset(&s_rend_cbs, 0, sizeof(s_rend_cbs));
  }
}

void bleVcpVendorSetControllerCbs(const ble_vcp_vendor_ctlr_cbs_t *cbs) {
  if (cbs) {
    s_ctlr_cbs = *cbs;
  } else {
    memset(&s_ctlr_cbs, 0, sizeof(s_ctlr_cbs));
  }
}

/* ── Volume Renderer (VCP server) ───────────────────────────────────────── */

#if BLE_AUDIO_VCP_RENDERER_SUPPORTED

static uint8_t s_rend_volume;
static uint8_t s_rend_mute;

static void rend_state_cb(esp_ble_conn_t *conn, int err, uint8_t volume, uint8_t mute) {
  (void)conn;
  if (err) {
    ESP_LOGE(VCP_TAG, "renderer state err %d", err);
    return;
  }
  s_rend_volume = volume;
  s_rend_mute = mute;
  if (s_rend_cbs.state) {
    s_rend_cbs.state(volume, mute);
  }
}

static void rend_flags_cb(esp_ble_conn_t *conn, int err, uint8_t flags) {
  (void)conn;
  (void)err;
  (void)flags;
}

static esp_ble_audio_vcp_vol_rend_cb_t s_rend_cb = {
  .state = rend_state_cb,
  .flags = rend_flags_cb,
};

/* Minimal included-service callbacks (spec requires the services to exist; the
 * transparent control demo only surfaces the top-level volume/mute). */
#if VCP_VOCS_INST_COUNT > 0
static void vocs_state_cb(esp_ble_audio_vocs_t *inst, int err, int16_t offset) {
  (void)inst;
  (void)err;
  (void)offset;
}
static esp_ble_audio_vocs_cb_t s_vocs_cb = {.state = vocs_state_cb};
static char s_vocs_desc[VCP_VOCS_INST_COUNT][16];
#endif

#if VCP_AICS_INST_COUNT > 0
static void aics_state_cb(esp_ble_audio_aics_t *inst, int err, int8_t gain, uint8_t mute, uint8_t mode) {
  (void)inst;
  (void)err;
  (void)gain;
  (void)mute;
  (void)mode;
}
static esp_ble_audio_aics_cb_t s_aics_cb = {.state = aics_state_cb};
static char s_aics_desc[VCP_AICS_INST_COUNT][16];
#endif

int bleVcpVendorRendererInit(uint8_t volume, uint8_t mute, uint8_t step) {
  esp_ble_audio_vcp_vol_rend_register_param_t param = {0};

#if VCP_VOCS_INST_COUNT > 0
  static esp_ble_audio_vocs_register_param_t vocs_param[VCP_VOCS_INST_COUNT];
  memset(vocs_param, 0, sizeof(vocs_param));
  for (size_t i = 0; i < VCP_VOCS_INST_COUNT; i++) {
    vocs_param[i].location_writable = true;
    vocs_param[i].desc_writable = true;
    snprintf(s_vocs_desc[i], sizeof(s_vocs_desc[i]), "Output %u", (unsigned)(i + 1));
    vocs_param[i].output_desc = s_vocs_desc[i];
    vocs_param[i].cb = &s_vocs_cb;
  }
  param.vocs_param = vocs_param;
#endif

#if VCP_AICS_INST_COUNT > 0
  static esp_ble_audio_aics_register_param_t aics_param[VCP_AICS_INST_COUNT];
  memset(aics_param, 0, sizeof(aics_param));
  for (size_t i = 0; i < VCP_AICS_INST_COUNT; i++) {
    aics_param[i].gain_mode = ESP_BLE_AUDIO_AICS_MODE_MANUAL;
    aics_param[i].units = 1;
    aics_param[i].min_gain = -100;
    aics_param[i].max_gain = 100;
    aics_param[i].type = ESP_BLE_AUDIO_AICS_INPUT_TYPE_UNSPECIFIED;
    aics_param[i].status = true;
    aics_param[i].desc_writable = true;
    snprintf(s_aics_desc[i], sizeof(s_aics_desc[i]), "Input %u", (unsigned)(i + 1));
    aics_param[i].description = s_aics_desc[i];
    aics_param[i].cb = &s_aics_cb;
  }
  param.aics_param = aics_param;
#endif

  param.step = step ? step : 1;
  param.mute = mute ? ESP_BLE_AUDIO_VCP_STATE_MUTED : ESP_BLE_AUDIO_VCP_STATE_UNMUTED;
  param.volume = volume;
  param.cb = &s_rend_cb;

  s_rend_volume = volume;
  s_rend_mute = mute;

  esp_err_t err = esp_ble_audio_vcp_vol_rend_register(&param);
  if (err) {
    ESP_LOGE(VCP_TAG, "vol_rend_register: %d", err);
    return (int)err;
  }
  ESP_LOGI(VCP_TAG, "volume renderer registered (vol=%u mute=%u vocs=%d aics=%d)", volume, mute, VCP_VOCS_INST_COUNT, VCP_AICS_INST_COUNT);
  return 0;
}

int bleVcpVendorRendererSetVolume(uint8_t volume) {
  return (int)esp_ble_audio_vcp_vol_rend_set_vol(volume);
}
int bleVcpVendorRendererSetMute(bool mute) {
  return (int)(mute ? esp_ble_audio_vcp_vol_rend_mute() : esp_ble_audio_vcp_vol_rend_unmute());
}
int bleVcpVendorRendererVolumeUp(void) {
  return (int)esp_ble_audio_vcp_vol_rend_vol_up();
}
int bleVcpVendorRendererVolumeDown(void) {
  return (int)esp_ble_audio_vcp_vol_rend_vol_down();
}
uint8_t bleVcpVendorRendererGetVolume(void) {
  return s_rend_volume;
}
bool bleVcpVendorRendererIsMuted(void) {
  return s_rend_mute != 0;
}

#else /* !BLE_AUDIO_VCP_RENDERER_SUPPORTED */

int bleVcpVendorRendererInit(uint8_t volume, uint8_t mute, uint8_t step) {
  (void)volume;
  (void)mute;
  (void)step;
  return -1;
}
int bleVcpVendorRendererSetVolume(uint8_t volume) {
  (void)volume;
  return -1;
}
int bleVcpVendorRendererSetMute(bool mute) {
  (void)mute;
  return -1;
}
int bleVcpVendorRendererVolumeUp(void) {
  return -1;
}
int bleVcpVendorRendererVolumeDown(void) {
  return -1;
}
uint8_t bleVcpVendorRendererGetVolume(void) {
  return 0;
}
bool bleVcpVendorRendererIsMuted(void) {
  return false;
}

#endif /* BLE_AUDIO_VCP_RENDERER_SUPPORTED */

/* ── Volume Controller (VCP client) ─────────────────────────────────────── */

#if BLE_AUDIO_VCP_CONTROLLER_SUPPORTED

static void ctlr_state_cb(esp_ble_audio_vcp_vol_ctlr_t *vol_ctlr, int err, uint8_t volume, uint8_t mute) {
  (void)vol_ctlr;
  if (s_ctlr_cbs.state) {
    s_ctlr_cbs.state(err, volume, mute);
  }
}

static void ctlr_discover_cb(esp_ble_audio_vcp_vol_ctlr_t *vol_ctlr, int err, uint8_t vocs_count, uint8_t aics_count) {
  (void)vol_ctlr;
  if (s_ctlr_cbs.discovered) {
    s_ctlr_cbs.discovered(err, vocs_count, aics_count);
  }
}

static esp_ble_audio_vcp_vol_ctlr_cb_t s_ctlr_cb = {
  .state = ctlr_state_cb,
  .discover = ctlr_discover_cb,
};

int bleVcpVendorControllerInit(void) {
  esp_err_t err = esp_ble_audio_vcp_vol_ctlr_cb_register(&s_ctlr_cb);
  if (err) {
    ESP_LOGE(VCP_TAG, "vol_ctlr_cb_register: %d", err);
    return (int)err;
  }
  ESP_LOGI(VCP_TAG, "volume controller registered");
  return 0;
}

int bleVcpVendorControllerDiscover(uint16_t conn_handle) {
  esp_ble_audio_vcp_vol_ctlr_t *vol_ctlr = NULL;
  esp_err_t err = esp_ble_audio_vcp_vol_ctlr_discover(conn_handle, &vol_ctlr);
  if (err) {
    ESP_LOGE(VCP_TAG, "vol_ctlr_discover: %d", err);
  }
  return (int)err;
}

static esp_ble_audio_vcp_vol_ctlr_t *ctlr_for(uint16_t conn_handle) {
  return esp_ble_audio_vcp_vol_ctlr_get_by_conn(conn_handle);
}

int bleVcpVendorControllerSetVolume(uint16_t conn_handle, uint8_t volume) {
  esp_ble_audio_vcp_vol_ctlr_t *c = ctlr_for(conn_handle);
  if (!c) {
    return -ENOTCONN;
  }
  return (int)esp_ble_audio_vcp_vol_ctlr_set_vol(c, volume);
}
int bleVcpVendorControllerSetMute(uint16_t conn_handle, bool mute) {
  esp_ble_audio_vcp_vol_ctlr_t *c = ctlr_for(conn_handle);
  if (!c) {
    return -ENOTCONN;
  }
  return (int)(mute ? esp_ble_audio_vcp_vol_ctlr_mute(c) : esp_ble_audio_vcp_vol_ctlr_unmute(c));
}
int bleVcpVendorControllerVolumeUp(uint16_t conn_handle) {
  esp_ble_audio_vcp_vol_ctlr_t *c = ctlr_for(conn_handle);
  if (!c) {
    return -ENOTCONN;
  }
  return (int)esp_ble_audio_vcp_vol_ctlr_vol_up(c);
}
int bleVcpVendorControllerVolumeDown(uint16_t conn_handle) {
  esp_ble_audio_vcp_vol_ctlr_t *c = ctlr_for(conn_handle);
  if (!c) {
    return -ENOTCONN;
  }
  return (int)esp_ble_audio_vcp_vol_ctlr_vol_down(c);
}
int bleVcpVendorControllerReadState(uint16_t conn_handle) {
  esp_ble_audio_vcp_vol_ctlr_t *c = ctlr_for(conn_handle);
  if (!c) {
    return -ENOTCONN;
  }
  return (int)esp_ble_audio_vcp_vol_ctlr_read_state(c);
}

#else /* !BLE_AUDIO_VCP_CONTROLLER_SUPPORTED */

int bleVcpVendorControllerInit(void) {
  return -1;
}
int bleVcpVendorControllerDiscover(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}
int bleVcpVendorControllerSetVolume(uint16_t conn_handle, uint8_t volume) {
  (void)conn_handle;
  (void)volume;
  return -1;
}
int bleVcpVendorControllerSetMute(uint16_t conn_handle, bool mute) {
  (void)conn_handle;
  (void)mute;
  return -1;
}
int bleVcpVendorControllerVolumeUp(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}
int bleVcpVendorControllerVolumeDown(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}
int bleVcpVendorControllerReadState(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}

#endif /* BLE_AUDIO_VCP_CONTROLLER_SUPPORTED */

void bleVcpVendorDeinit(void) {
  memset(&s_rend_cbs, 0, sizeof(s_rend_cbs));
  memset(&s_ctlr_cbs, 0, sizeof(s_ctlr_cbs));
}

#endif /* BLE_AUDIO_SUPPORTED */
