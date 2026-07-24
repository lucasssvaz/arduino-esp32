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
 * @file BLEAudioBapVendor.c
 * @brief C language boundary for ESP-BLE-AUDIO BAP unicast calls.
 *
 * Compiled as C so the vendor headers (and the Zephyr/GNU-C capability + preset
 * macros they use) parse correctly. This is the ONLY translation unit that names
 * `esp_ble_audio_bap_*` / `esp_ble_audio_pacs_*` / `esp_ble_audio_codec_*`
 * types; everything above it goes through the C-safe `BLEAudioBapVendor.h`
 * surface. It owns the BAP stream objects and condenses the vendor unicast
 * server / unicast client example orchestration to a single unicast pair.
 *
 * Direction is data-flow-relative to the local device (see the header):
 *   DIR_SOURCE = local transmits SDUs, DIR_SINK = local receives SDUs.
 * On the client, the vendor "sink" endpoint (which the client transmits to) is
 * therefore mapped to the local TX (DIR_SOURCE) stream, and the vendor "source"
 * endpoint to the local RX (DIR_SINK) stream, so the C++ layer's send/receive
 * semantics are identical on both roles.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioBapVendor.h"

#include "esp_ble_audio_lc3_defs.h"
#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_bap_lc3_preset_defs.h"
#include "esp_ble_audio_pacs_api.h"
#include "esp_ble_audio_codec_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_audio_common_api.h"
#include "esp_log.h"

#include <string.h>
#include <errno.h>

static const char *BAP_TAG = "BLEAudioBap";

/* ASE-count Kconfig fallbacks (defined when the roles are enabled; guard in case
 * a build enables the API without the per-role count symbol). */
#ifndef CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT
#define CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT 1
#endif
#ifndef CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT
#define CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT 1
#endif

/* ── Dispatch into C++ (set once before init) ───────────────────────────── */

static ble_bap_vendor_stream_cbs_t s_cbs;

static void dispatch_started(uint8_t dir) {
  if (s_cbs.started) {
    s_cbs.started(dir);
  }
}
static void dispatch_stopped(uint8_t dir, uint8_t reason) {
  if (s_cbs.stopped) {
    s_cbs.stopped(dir, reason);
  }
}
static void dispatch_recv(uint8_t dir, const esp_ble_iso_recv_info_t *info, const uint8_t *data, uint16_t len) {
  if (!s_cbs.recv) {
    return;
  }
  ble_bap_vendor_recv_info_t out = {0};
  if (info) {
    out.timestamp_us = info->ts;
    out.seq_num = info->seq_num;
    out.valid = (info->flags & ESP_BLE_ISO_FLAGS_VALID) != 0;
    out.ts_valid = (info->flags & ESP_BLE_ISO_FLAGS_TS) != 0;
  }
  s_cbs.recv(dir, &out, data, len);
}
static void dispatch_sent(uint8_t dir) {
  if (s_cbs.sent) {
    s_cbs.sent(dir);
  }
}

void bleBapVendorSetStreamCbs(const ble_bap_vendor_stream_cbs_t *cbs) {
  if (cbs) {
    s_cbs = *cbs;
  } else {
    memset(&s_cbs, 0, sizeof(s_cbs));
  }
}

/* ── Default published LC3 capability (covers every mandatory preset) ────── */

/* Advertise the LC3_16_2_1 capability only (16 kHz, 10 ms, mono+stereo,
 * 40 octets). A wide FREQ_ANY/DURATION_ANY PAC made PipeWire SelectProperties
 * pick 32 kHz / 7.5 ms, which never progressed to a MediaTransport against our
 * LC3_16_2_1 player/recorder path. */
static uint8_t s_cap_data[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_DATA(
  ESP_BLE_AUDIO_CODEC_CAP_FREQ_16KHZ, ESP_BLE_AUDIO_CODEC_CAP_DURATION_10, ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1, 2), 40, 40,
  1
);

/* Preferred-context hint in the published PAC record; the authoritative
 * available/supported contexts are set per-caller via the PACS setters below. */
static uint8_t s_cap_meta[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_META(
  ESP_BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED | ESP_BLE_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA
);

static const esp_ble_audio_codec_cap_t s_codec_cap = ESP_BLE_AUDIO_CODEC_CAP_LC3(s_cap_data, s_cap_meta);
static esp_ble_audio_pacs_cap_t s_pac_sink = {.codec_cap = &s_codec_cap};
static esp_ble_audio_pacs_cap_t s_pac_source = {.codec_cap = &s_codec_cap};

static const esp_ble_audio_bap_qos_cfg_pref_t s_qos_pref = ESP_BLE_AUDIO_BAP_QOS_CFG_PREF(
  true, ESP_BLE_ISO_PHY_2M, 2, 10, 20000, 40000, 20000, 40000
);

/* ── LC3 preset table (client) ──────────────────────────────────────────── */

ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_16_2_1_DEFINE(s_preset_16_2_1, ESP_BLE_AUDIO_LOCATION_FRONT_LEFT, ESP_BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_24_2_1_DEFINE(s_preset_24_2_1, ESP_BLE_AUDIO_LOCATION_FRONT_LEFT, ESP_BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_4_1_DEFINE(s_preset_48_4_1, ESP_BLE_AUDIO_LOCATION_FRONT_LEFT, ESP_BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED);

static esp_ble_audio_bap_lc3_preset_t *preset_for(ble_bap_vendor_preset_t sel) {
  switch (sel) {
    case BLE_BAP_VENDOR_PRESET_24_2_1: return &s_preset_24_2_1;
    case BLE_BAP_VENDOR_PRESET_48_4_1: return &s_preset_48_4_1;
    case BLE_BAP_VENDOR_PRESET_16_2_1:
    default: return &s_preset_16_2_1;
  }
}

/* ── Role state ─────────────────────────────────────────────────────────── */

typedef enum { ROLE_NONE = 0, ROLE_SERVER, ROLE_CLIENT } bap_role_t;
static bap_role_t s_role;

/* Server owns one sink (RX) + one source (TX) stream. */
static esp_ble_audio_bap_stream_t s_srv_sink;
static esp_ble_audio_bap_stream_t s_srv_source;
static bool s_srv_sink_streaming;
static bool s_srv_source_streaming;

/* Client owns one TX stream (to peer sink) + one RX stream (from peer source). */
static esp_ble_audio_bap_stream_t s_cli_tx;   /* vendor "sink" endpoint */
static esp_ble_audio_bap_stream_t s_cli_rx;   /* vendor "source" endpoint */
static esp_ble_audio_bap_ep_t *s_cli_tx_ep;
static esp_ble_audio_bap_ep_t *s_cli_rx_ep;
static esp_ble_audio_bap_unicast_group_t *s_cli_group;
static uint16_t s_cli_conn = 0xFFFF;
static ble_bap_vendor_preset_t s_cli_preset;
static bool s_cli_tx_streaming;
static bool s_cli_rx_streaming;

/* Client stream setup progress flags. */
static bool s_cli_tx_configured, s_cli_rx_configured;
static bool s_cli_qos_set;
static bool s_cli_tx_enabled, s_cli_rx_enabled;
static bool s_cli_connected;

/* ── Server: ASE-control callbacks ──────────────────────────────────────── */

static esp_ble_audio_bap_stream_t *srv_alloc(esp_ble_audio_dir_t dir) {
  if (dir == ESP_BLE_AUDIO_DIR_SOURCE) {
    return s_srv_source.conn == NULL ? &s_srv_source : NULL;
  }
  return s_srv_sink.conn == NULL ? &s_srv_sink : NULL;
}

static int srv_config_cb(
  esp_ble_conn_t *conn, const esp_ble_audio_bap_ep_t *ep, esp_ble_audio_dir_t dir, const esp_ble_audio_codec_cfg_t *codec_cfg,
  esp_ble_audio_bap_stream_t **stream, esp_ble_audio_bap_qos_cfg_pref_t *const pref, esp_ble_audio_bap_ascs_rsp_t *rsp
) {
  (void)conn;
  (void)ep;
  (void)codec_cfg;
  *stream = srv_alloc(dir);
  if (*stream == NULL) {
    *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_NO_MEM, ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
    return -ENOMEM;
  }
  *pref = s_qos_pref;
  return 0;
}

static int srv_reconfig_cb(
  esp_ble_audio_bap_stream_t *stream, esp_ble_audio_dir_t dir, const esp_ble_audio_codec_cfg_t *codec_cfg, esp_ble_audio_bap_qos_cfg_pref_t *const pref,
  esp_ble_audio_bap_ascs_rsp_t *rsp
) {
  (void)stream;
  (void)dir;
  (void)codec_cfg;
  (void)pref;
  *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_CONF_UNSUPPORTED, ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
  return -ENOEXEC;
}

static int srv_qos_cb(esp_ble_audio_bap_stream_t *stream, const esp_ble_audio_bap_qos_cfg_t *qos, esp_ble_audio_bap_ascs_rsp_t *rsp) {
  (void)stream;
  (void)qos;
  (void)rsp;
  return 0;
}

static int srv_enable_cb(esp_ble_audio_bap_stream_t *stream, const uint8_t meta[], size_t meta_len, esp_ble_audio_bap_ascs_rsp_t *rsp) {
  (void)stream;
  (void)meta;
  (void)meta_len;
  (void)rsp;
  return 0;
}

static int srv_start_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_t *rsp) {
  (void)stream;
  (void)rsp;
  return 0;
}

static int srv_metadata_cb(esp_ble_audio_bap_stream_t *stream, const uint8_t meta[], size_t meta_len, esp_ble_audio_bap_ascs_rsp_t *rsp) {
  (void)stream;
  (void)meta;
  (void)meta_len;
  (void)rsp;
  return 0;
}

static int srv_disable_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_t *rsp) {
  (void)stream;
  (void)rsp;
  return 0;
}
static int srv_stop_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_t *rsp) {
  (void)stream;
  (void)rsp;
  return 0;
}
static int srv_release_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_t *rsp) {
  (void)stream;
  (void)rsp;
  return 0;
}

static const esp_ble_audio_bap_unicast_server_cb_t s_srv_cb = {
  .config = srv_config_cb,
  .reconfig = srv_reconfig_cb,
  .qos = srv_qos_cb,
  .enable = srv_enable_cb,
  .start = srv_start_cb,
  .metadata = srv_metadata_cb,
  .disable = srv_disable_cb,
  .stop = srv_stop_cb,
  .release = srv_release_cb,
};

/* ── Server: per-stream ops ─────────────────────────────────────────────── */

static bool srv_is_source(const esp_ble_audio_bap_stream_t *s) {
  return s == &s_srv_source;
}

static void srv_stream_enabled(esp_ble_audio_bap_stream_t *stream) {
  /* The unicast server starts its own sink ASEs once the client enables them. */
  if (!srv_is_source(stream)) {
    esp_err_t err = esp_ble_audio_bap_stream_start(stream);
    if (err != ESP_OK) {
      ESP_LOGE(BAP_TAG, "server sink start failed: %d", err);
    }
  }
}

static void srv_stream_started(esp_ble_audio_bap_stream_t *stream) {
  if (srv_is_source(stream)) {
    s_srv_source_streaming = true;
    dispatch_started(BLE_BAP_VENDOR_DIR_SOURCE);
  } else {
    s_srv_sink_streaming = true;
    dispatch_started(BLE_BAP_VENDOR_DIR_SINK);
  }
}

static void srv_stream_stopped(esp_ble_audio_bap_stream_t *stream, uint8_t reason) {
  if (srv_is_source(stream)) {
    s_srv_source_streaming = false;
    dispatch_stopped(BLE_BAP_VENDOR_DIR_SOURCE, reason);
  } else {
    s_srv_sink_streaming = false;
    dispatch_stopped(BLE_BAP_VENDOR_DIR_SINK, reason);
  }
}

static void srv_stream_recv(esp_ble_audio_bap_stream_t *stream, const esp_ble_iso_recv_info_t *info, const uint8_t *data, uint16_t len) {
  if (!srv_is_source(stream)) {
    dispatch_recv(BLE_BAP_VENDOR_DIR_SINK, info, data, len);
  }
}

static void srv_stream_sent(esp_ble_audio_bap_stream_t *stream, void *user_data) {
  (void)user_data;
  if (srv_is_source(stream)) {
    dispatch_sent(BLE_BAP_VENDOR_DIR_SOURCE);
  }
}

static esp_ble_audio_bap_stream_ops_t s_srv_ops = {
  .enabled = srv_stream_enabled,
  .started = srv_stream_started,
  .stopped = srv_stream_stopped,
  .recv = srv_stream_recv,
  .sent = srv_stream_sent,
};

int bleBapVendorServerInit(bool sink, bool source, uint16_t sink_ctx, uint16_t src_ctx, uint32_t sink_loc, uint32_t src_loc) {
  esp_err_t err;
  const esp_ble_audio_pacs_register_param_t pacs_param = {
    .snk_pac = sink,
    .snk_loc = sink,
    .src_pac = source,
    .src_loc = source,
  };
  esp_ble_audio_bap_unicast_server_register_param_t reg = {
    CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT,
    CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT,
  };

  err = esp_ble_audio_pacs_register(&pacs_param);
  if (err) {
    ESP_LOGE(BAP_TAG, "pacs_register: %d", err);
    return (int)err;
  }
  err = esp_ble_audio_bap_unicast_server_register(&reg);
  if (err) {
    ESP_LOGE(BAP_TAG, "server_register: %d", err);
    return (int)err;
  }
  err = esp_ble_audio_bap_unicast_server_register_cb(&s_srv_cb);
  if (err) {
    ESP_LOGE(BAP_TAG, "server_register_cb: %d", err);
    return (int)err;
  }
  if (sink) {
    err = esp_ble_audio_pacs_cap_register(ESP_BLE_AUDIO_DIR_SINK, &s_pac_sink);
    if (err) {
      return (int)err;
    }
  }
  if (source) {
    err = esp_ble_audio_pacs_cap_register(ESP_BLE_AUDIO_DIR_SOURCE, &s_pac_source);
    if (err) {
      return (int)err;
    }
  }

  esp_ble_audio_bap_stream_cb_register(&s_srv_sink, &s_srv_ops);
  esp_ble_audio_bap_stream_cb_register(&s_srv_source, &s_srv_ops);

  if (sink) {
    esp_ble_audio_pacs_set_location(ESP_BLE_AUDIO_DIR_SINK, sink_loc);
    esp_ble_audio_pacs_set_supported_contexts(ESP_BLE_AUDIO_DIR_SINK, sink_ctx);
    esp_ble_audio_pacs_set_available_contexts(ESP_BLE_AUDIO_DIR_SINK, sink_ctx);
  }
  if (source) {
    esp_ble_audio_pacs_set_location(ESP_BLE_AUDIO_DIR_SOURCE, src_loc);
    esp_ble_audio_pacs_set_supported_contexts(ESP_BLE_AUDIO_DIR_SOURCE, src_ctx);
    esp_ble_audio_pacs_set_available_contexts(ESP_BLE_AUDIO_DIR_SOURCE, src_ctx);
  }

  s_role = ROLE_SERVER;
  ESP_LOGI(BAP_TAG, "unicast server registered (snk=%d src=%d)", (int)sink, (int)source);
  return 0;
}

/* ── Client: orchestration ──────────────────────────────────────────────── */

static void cli_reset_progress(void) {
  s_cli_tx_ep = NULL;
  s_cli_rx_ep = NULL;
  s_cli_tx_configured = s_cli_rx_configured = false;
  s_cli_qos_set = false;
  s_cli_tx_enabled = s_cli_rx_enabled = false;
  s_cli_connected = false;
  s_cli_tx_streaming = s_cli_rx_streaming = false;
}

static bool cli_is_tx(const esp_ble_audio_bap_stream_t *s) {
  return s == &s_cli_tx;
}

/* Configure whichever discovered endpoint has not been configured yet.
 * Returns true when there is nothing left to configure. */
static bool cli_configure_next(void) {
  esp_ble_audio_bap_lc3_preset_t *preset = preset_for(s_cli_preset);
  if (s_cli_tx_ep != NULL && !s_cli_tx_configured) {
    int err = esp_ble_audio_bap_stream_config(s_cli_conn, &s_cli_tx, s_cli_tx_ep, &preset->codec_cfg);
    if (err) {
      ESP_LOGE(BAP_TAG, "tx config: %d", err);
      s_cli_tx_ep = NULL;  /* give up on this ep */
    } else {
      return false;
    }
  }
  if (s_cli_rx_ep != NULL && !s_cli_rx_configured) {
    int err = esp_ble_audio_bap_stream_config(s_cli_conn, &s_cli_rx, s_cli_rx_ep, &preset->codec_cfg);
    if (err) {
      ESP_LOGE(BAP_TAG, "rx config: %d", err);
      s_cli_rx_ep = NULL;
    } else {
      return false;
    }
  }
  return true;
}

static int cli_create_group_and_qos(void) {
  esp_ble_audio_bap_lc3_preset_t *preset = preset_for(s_cli_preset);
  esp_ble_audio_bap_unicast_group_stream_param_t tx_sp = {0};
  esp_ble_audio_bap_unicast_group_stream_param_t rx_sp = {0};
  esp_ble_audio_bap_unicast_group_stream_pair_param_t pair = {0};
  esp_ble_audio_bap_unicast_group_param_t group_param = {0};
  int err;

  if (s_cli_tx_configured) {
    tx_sp.stream = &s_cli_tx;
    tx_sp.qos = &preset->qos;
    pair.tx_param = &tx_sp;
  }
  if (s_cli_rx_configured) {
    rx_sp.stream = &s_cli_rx;
    rx_sp.qos = &preset->qos;
    pair.rx_param = &rx_sp;
  }

  group_param.params = &pair;
  group_param.params_count = 1;
  group_param.packing = ESP_BLE_ISO_PACKING_SEQUENTIAL;

  err = esp_ble_audio_bap_unicast_group_create(&group_param, &s_cli_group);
  if (err) {
    ESP_LOGE(BAP_TAG, "group_create: %d", err);
    return err;
  }
  err = esp_ble_audio_bap_stream_qos(s_cli_conn, s_cli_group);
  if (err) {
    ESP_LOGE(BAP_TAG, "stream_qos: %d", err);
    return err;
  }
  return 0;
}

static bool cli_enable_next(void) {
  esp_ble_audio_bap_lc3_preset_t *preset = preset_for(s_cli_preset);
  if (s_cli_tx_configured && !s_cli_tx_enabled) {
    int err = esp_ble_audio_bap_stream_enable(&s_cli_tx, preset->codec_cfg.meta, preset->codec_cfg.meta_len);
    if (err) {
      ESP_LOGE(BAP_TAG, "tx enable: %d", err);
      s_cli_tx_enabled = true;  /* skip */
    } else {
      return false;
    }
  }
  if (s_cli_rx_configured && !s_cli_rx_enabled) {
    int err = esp_ble_audio_bap_stream_enable(&s_cli_rx, preset->codec_cfg.meta, preset->codec_cfg.meta_len);
    if (err) {
      ESP_LOGE(BAP_TAG, "rx enable: %d", err);
      s_cli_rx_enabled = true;
    } else {
      return false;
    }
  }
  return true;
}

static void cli_connect_cis(void) {
  if (s_cli_connected) {
    return;
  }
  esp_ble_audio_bap_stream_t *s = s_cli_tx_configured ? &s_cli_tx : (s_cli_rx_configured ? &s_cli_rx : NULL);
  if (s == NULL) {
    return;
  }
  int err = esp_ble_audio_bap_stream_connect(s);
  if (err) {
    ESP_LOGE(BAP_TAG, "stream_connect: %d", err);
  }
}

static void cli_start_source_streams(void) {
  /* Sink (peer) streams are started by the server; the client starts the
   * streams it sources from (its RX from the peer's source ASE). */
  if (s_cli_rx_configured) {
    (void)esp_ble_audio_bap_stream_start(&s_cli_rx);
  }
}

/* Client discovery/ASCS-ACK callbacks */

static void cli_discover_cb(esp_ble_conn_t *conn, int err, esp_ble_audio_dir_t dir) {
  if (conn->handle != s_cli_conn) {
    return;
  }
  if (dir == ESP_BLE_AUDIO_DIR_SINK) {
    if (!err) {
      (void)esp_ble_audio_bap_unicast_client_discover(s_cli_conn, ESP_BLE_AUDIO_DIR_SOURCE);
    }
  } else {
    if (!err) {
      cli_configure_next();
    }
  }
}

static void cli_endpoint_cb(esp_ble_conn_t *conn, esp_ble_audio_dir_t dir, esp_ble_audio_bap_ep_t *ep) {
  (void)conn;
  if (dir == ESP_BLE_AUDIO_DIR_SINK) {
    if (s_cli_tx_ep == NULL) {
      s_cli_tx_ep = ep;  /* peer sink -> local TX */
    }
  } else {
    if (s_cli_rx_ep == NULL) {
      s_cli_rx_ep = ep;  /* peer source -> local RX */
    }
  }
}

static void cli_config_ack_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_code_t rsp, esp_ble_audio_bap_ascs_reason_t reason) {
  (void)reason;
  bool ok = (rsp == ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_SUCCESS);
  if (cli_is_tx(stream)) {
    s_cli_tx_configured = ok;
  } else {
    s_cli_rx_configured = ok;
  }
  if (!cli_configure_next()) {
    return;
  }
  if (!s_cli_tx_configured && !s_cli_rx_configured) {
    ESP_LOGW(BAP_TAG, "no streams configured");
    return;
  }
  cli_create_group_and_qos();
}

static void cli_qos_ack_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_code_t rsp, esp_ble_audio_bap_ascs_reason_t reason) {
  (void)stream;
  (void)reason;
  if (rsp != ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_SUCCESS) {
    return;
  }
  /* QoS is applied to the whole group at once; enable after the first ACK. */
  if (!s_cli_qos_set) {
    s_cli_qos_set = true;
    cli_enable_next();
  }
}

static void cli_enable_ack_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_code_t rsp, esp_ble_audio_bap_ascs_reason_t reason) {
  (void)reason;
  bool ok = (rsp == ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_SUCCESS);
  if (cli_is_tx(stream)) {
    s_cli_tx_enabled = ok;
  } else {
    s_cli_rx_enabled = ok;
  }
  if (!cli_enable_next()) {
    return;
  }
  cli_connect_cis();
}

static void cli_generic_ack_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_code_t rsp, esp_ble_audio_bap_ascs_reason_t reason) {
  (void)stream;
  (void)rsp;
  (void)reason;
}

static esp_ble_audio_bap_unicast_client_cb_t s_cli_cb = {
  .config = cli_config_ack_cb,
  .qos = cli_qos_ack_cb,
  .enable = cli_enable_ack_cb,
  .start = cli_generic_ack_cb,
  .stop = cli_generic_ack_cb,
  .disable = cli_generic_ack_cb,
  .metadata = cli_generic_ack_cb,
  .release = cli_generic_ack_cb,
  .endpoint = cli_endpoint_cb,
  .discover = cli_discover_cb,
};

/* Client per-stream ops (ISO connect/start/recv/sent) */

static void cli_stream_connected(esp_ble_audio_bap_stream_t *stream) {
  (void)stream;
  s_cli_connected = true;
  cli_start_source_streams();
}

static void cli_stream_disconnected(esp_ble_audio_bap_stream_t *stream, uint8_t reason) {
  (void)stream;
  (void)reason;
  s_cli_connected = false;
}

static void cli_stream_started(esp_ble_audio_bap_stream_t *stream) {
  if (cli_is_tx(stream)) {
    s_cli_tx_streaming = true;
    dispatch_started(BLE_BAP_VENDOR_DIR_SOURCE);
  } else {
    s_cli_rx_streaming = true;
    dispatch_started(BLE_BAP_VENDOR_DIR_SINK);
  }
}

static void cli_stream_stopped(esp_ble_audio_bap_stream_t *stream, uint8_t reason) {
  if (cli_is_tx(stream)) {
    s_cli_tx_streaming = false;
    dispatch_stopped(BLE_BAP_VENDOR_DIR_SOURCE, reason);
  } else {
    s_cli_rx_streaming = false;
    dispatch_stopped(BLE_BAP_VENDOR_DIR_SINK, reason);
  }
}

static void cli_stream_recv(esp_ble_audio_bap_stream_t *stream, const esp_ble_iso_recv_info_t *info, const uint8_t *data, uint16_t len) {
  if (!cli_is_tx(stream)) {
    dispatch_recv(BLE_BAP_VENDOR_DIR_SINK, info, data, len);
  }
}

static void cli_stream_sent(esp_ble_audio_bap_stream_t *stream, void *user_data) {
  (void)user_data;
  if (cli_is_tx(stream)) {
    dispatch_sent(BLE_BAP_VENDOR_DIR_SOURCE);
  }
}

static esp_ble_audio_bap_stream_ops_t s_cli_ops = {
  .started = cli_stream_started,
  .stopped = cli_stream_stopped,
  .recv = cli_stream_recv,
  .sent = cli_stream_sent,
  .connected = cli_stream_connected,
  .disconnected = cli_stream_disconnected,
};

int bleBapVendorClientInit(void) {
  esp_err_t err;
  s_cli_tx.ops = &s_cli_ops;
  s_cli_rx.ops = &s_cli_ops;
  err = esp_ble_audio_bap_unicast_client_register_cb(&s_cli_cb);
  if (err) {
    ESP_LOGE(BAP_TAG, "client_register_cb: %d", err);
    return (int)err;
  }
  s_role = ROLE_CLIENT;
  cli_reset_progress();
  ESP_LOGI(BAP_TAG, "unicast client registered");
  return 0;
}

int bleBapVendorClientStart(uint16_t conn_handle, ble_bap_vendor_preset_t preset) {
  if (s_role != ROLE_CLIENT) {
    return -1;
  }
  s_cli_conn = conn_handle;
  s_cli_preset = preset;
  cli_reset_progress();
  int err = esp_ble_audio_bap_unicast_client_discover(conn_handle, ESP_BLE_AUDIO_DIR_SINK);
  if (err) {
    ESP_LOGE(BAP_TAG, "discover: %d", err);
  }
  return err;
}

void bleBapVendorClientReset(void) {
  if (s_cli_group != NULL) {
    esp_ble_audio_bap_unicast_group_delete(s_cli_group);
    s_cli_group = NULL;
  }
  esp_ble_audio_bap_stream_ops_t *tx_ops = s_cli_tx.ops;
  esp_ble_audio_bap_stream_ops_t *rx_ops = s_cli_rx.ops;
  memset(&s_cli_tx, 0, sizeof(s_cli_tx));
  memset(&s_cli_rx, 0, sizeof(s_cli_rx));
  s_cli_tx.ops = tx_ops;
  s_cli_rx.ops = rx_ops;
  s_cli_conn = 0xFFFF;
  cli_reset_progress();
}

/* ── Shared TX + queries ────────────────────────────────────────────────── */

int bleBapVendorStreamSend(uint8_t dir, const uint8_t *sdu, uint16_t len, uint16_t seq_num) {
  if (dir != BLE_BAP_VENDOR_DIR_SOURCE) {
    return -1;  /* sink is RX-only */
  }
  esp_ble_audio_bap_stream_t *s = NULL;
  if (s_role == ROLE_SERVER) {
    s = &s_srv_source;
  } else if (s_role == ROLE_CLIENT) {
    s = &s_cli_tx;
  }
  if (s == NULL) {
    return -1;
  }
  return (int)esp_ble_audio_bap_stream_send(s, sdu, len, seq_num);
}

bool bleBapVendorStreamIsStreaming(uint8_t dir) {
  if (s_role == ROLE_SERVER) {
    return dir == BLE_BAP_VENDOR_DIR_SOURCE ? s_srv_source_streaming : s_srv_sink_streaming;
  }
  if (s_role == ROLE_CLIENT) {
    return dir == BLE_BAP_VENDOR_DIR_SOURCE ? s_cli_tx_streaming : s_cli_rx_streaming;
  }
  return false;
}

void bleBapVendorDeinit(void) {
  if (s_role == ROLE_CLIENT) {
    bleBapVendorClientReset();
  }
  s_srv_sink_streaming = false;
  s_srv_source_streaming = false;
  s_role = ROLE_NONE;
}

#endif /* BLE_AUDIO_SUPPORTED */
