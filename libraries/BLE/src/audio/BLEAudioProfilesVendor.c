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
 * @file BLEAudioProfilesVendor.c
 * @brief C language boundary for ESP-BLE-AUDIO TMAP/GMAP identity services.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioProfilesVendor.h"

#include "esp_log.h"

#include <errno.h>
#include <string.h>

__attribute__((unused)) static const char *PROFILES_TAG = "BLEAudioProfiles";

/* ── TMAP ────────────────────────────────────────────────────────────────── */

#if BLE_AUDIO_TMAP_SUPPORTED

#include "esp_ble_audio_tmap_api.h"

static ble_tmap_vendor_disc_fn s_tmap_disc;

void bleTmapVendorSetDiscCb(ble_tmap_vendor_disc_fn fn) {
  s_tmap_disc = fn;
}

static void tmap_discovery_complete_cb(enum bt_tmap_role role, struct bt_conn *conn, int err) {
  (void)conn;
  if (s_tmap_disc) {
    s_tmap_disc(err, (uint8_t)role);
  }
}

static const esp_ble_audio_tmap_cb_t s_tmap_cb = {
  .discovery_complete = tmap_discovery_complete_cb,
};

int bleTmapVendorRegister(uint8_t roles) {
  esp_err_t err = esp_ble_audio_tmap_register((esp_ble_audio_tmap_role_t)roles);
  if (err) {
    ESP_LOGE(PROFILES_TAG, "tmap_register(0x%02x): %d", roles, err);
    return (int)err;
  }
  ESP_LOGI(PROFILES_TAG, "TMAP registered (roles=0x%02x)", roles);
  return 0;
}

int bleTmapVendorDiscover(uint16_t conn_handle) {
  esp_err_t err = esp_ble_audio_tmap_discover(conn_handle, &s_tmap_cb);
  if (err) {
    ESP_LOGE(PROFILES_TAG, "tmap_discover: %d", err);
  }
  return (int)err;
}

#else /* !BLE_AUDIO_TMAP_SUPPORTED */

void bleTmapVendorSetDiscCb(ble_tmap_vendor_disc_fn fn) {
  (void)fn;
}
int bleTmapVendorRegister(uint8_t roles) {
  (void)roles;
  return -1;
}
int bleTmapVendorDiscover(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}

#endif /* BLE_AUDIO_TMAP_SUPPORTED */

/* ── GMAP ────────────────────────────────────────────────────────────────── */

#if BLE_AUDIO_GMAP_SUPPORTED

#include "esp_ble_audio_gmap_api.h"

static ble_gmap_vendor_disc_fn s_gmap_disc;

void bleGmapVendorSetDiscCb(ble_gmap_vendor_disc_fn fn) {
  s_gmap_disc = fn;
}

static void gmap_discover_cb(struct bt_conn *conn, int err, enum bt_gmap_role role, struct bt_gmap_feat features) {
  (void)conn;
  (void)features;
  if (s_gmap_disc) {
    s_gmap_disc(err, (uint8_t)role);
  }
}

static const esp_ble_audio_gmap_cb_t s_gmap_cb = {
  .discover = gmap_discover_cb,
};

int bleGmapVendorRegister(uint8_t roles, uint8_t ugg_feat, uint8_t ugt_feat, uint8_t bgs_feat, uint8_t bgr_feat) {
  (void)ugg_feat;
  (void)ugt_feat;
  (void)bgs_feat;
  (void)bgr_feat;
  /* Packaged Arduino libs track release/v6.1: GMAP API + lib exist, but there
   * is no host-adapter gmas.c, so GMAS never reaches the GATT table. Do not
   * call esp_ble_audio_gmap_register — that would only mutate local engine
   * bookkeeping while advertising a false "publish" success. Stub until libs
   * ship an IDF that includes the adapter (present on upstream master). */
  ESP_LOGW(PROFILES_TAG, "GMAP server stubbed (no GMAS on packaged IDF); roles=0x%02x ignored", roles);
  return -ENOTSUP;
}

int bleGmapVendorDiscover(uint16_t conn_handle) {
  static bool s_cb_registered = false;
  if (!s_cb_registered) {
    esp_err_t cbe = esp_ble_audio_gmap_cb_register(&s_gmap_cb);
    if (cbe) {
      ESP_LOGE(PROFILES_TAG, "gmap_cb_register: %d", cbe);
      return (int)cbe;
    }
    s_cb_registered = true;
  }
  esp_err_t err = esp_ble_audio_gmap_discover(conn_handle);
  if (err) {
    ESP_LOGE(PROFILES_TAG, "gmap_discover: %d", err);
  }
  return (int)err;
}

#else /* !BLE_AUDIO_GMAP_SUPPORTED */

void bleGmapVendorSetDiscCb(ble_gmap_vendor_disc_fn fn) {
  (void)fn;
}
int bleGmapVendorRegister(uint8_t roles, uint8_t ugg_feat, uint8_t ugt_feat, uint8_t bgs_feat, uint8_t bgr_feat) {
  (void)roles;
  (void)ugg_feat;
  (void)ugt_feat;
  (void)bgs_feat;
  (void)bgr_feat;
  return -1;
}
int bleGmapVendorDiscover(uint16_t conn_handle) {
  (void)conn_handle;
  return -1;
}

#endif /* BLE_AUDIO_GMAP_SUPPORTED */

/* ── PBP (Public Broadcast Announcement helper) ─────────────────────────── */

#if BLE_AUDIO_PBP_SUPPORTED

#include "esp_ble_audio_pbp_api.h"
#include <zephyr/net_buf.h>

#define BLE_PBP_VENDOR_SVC_DATA16 0x16 /* BT_DATA_SVC_DATA16 */

int blePbpVendorBuildAnnouncement(const uint8_t *meta, uint16_t meta_len, uint8_t features, uint8_t *out, uint16_t out_cap) {
  /* Bound metadata so the fixed 64-byte staging net_buf cannot overflow (a few
   * bytes go to the PBA header before the metadata). */
  if (meta_len > 48) {
    ESP_LOGE(PROFILES_TAG, "pbp metadata too long: %u", (unsigned)meta_len);
    return -1;
  }
  NET_BUF_SIMPLE_DEFINE(buf, 64);
  esp_err_t err = esp_ble_audio_pbp_get_announcement(meta, meta_len, (esp_ble_audio_pbp_announcement_feature_t)features, &buf);
  if (err) {
    ESP_LOGE(PROFILES_TAG, "pbp_get_announcement: %d", err);
    return (int)-err;
  }
  if (buf.len > out_cap) {
    return -1;
  }
  memcpy(out, buf.data, buf.len);
  return (int)buf.len;
}

int blePbpVendorParseAnnouncement(const uint8_t *data, uint8_t data_len, uint8_t *features_out, const uint8_t **meta_out, uint8_t *meta_len_out) {
  esp_ble_audio_pbp_announcement_feature_t feat = 0;
  uint8_t *meta = NULL;
  uint8_t mlen = 0;
  esp_err_t err = esp_ble_audio_pbp_parse_announcement(BLE_PBP_VENDOR_SVC_DATA16, data, data_len, &feat, &meta, &mlen);
  if (err) {
    return (int)-err;
  }
  if (features_out) {
    *features_out = (uint8_t)feat;
  }
  if (meta_out) {
    *meta_out = meta;
  }
  if (meta_len_out) {
    *meta_len_out = mlen;
  }
  return 0;
}

#else /* !BLE_AUDIO_PBP_SUPPORTED */

int blePbpVendorBuildAnnouncement(const uint8_t *meta, uint16_t meta_len, uint8_t features, uint8_t *out, uint16_t out_cap) {
  (void)meta;
  (void)meta_len;
  (void)features;
  (void)out;
  (void)out_cap;
  return -1;
}
int blePbpVendorParseAnnouncement(const uint8_t *data, uint8_t data_len, uint8_t *features_out, const uint8_t **meta_out, uint8_t *meta_len_out) {
  (void)data;
  (void)data_len;
  (void)features_out;
  (void)meta_out;
  (void)meta_len_out;
  return -1;
}

#endif /* BLE_AUDIO_PBP_SUPPORTED */

void bleProfilesVendorDeinit(void) {
  bleTmapVendorSetDiscCb(NULL);
  bleGmapVendorSetDiscCb(NULL);
}

#endif /* BLE_AUDIO_SUPPORTED */
