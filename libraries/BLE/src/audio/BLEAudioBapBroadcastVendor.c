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
 * @file BLEAudioBapBroadcastVendor.c
 * @brief C language boundary for the ESP-BLE-AUDIO BAP Broadcast Source.
 *
 * Compiled as C so the vendor headers + Zephyr/GNU-C preset macros parse. Owns
 * a single-subgroup, single-stream (mono) Broadcast Source and its BASE
 * encoding. See the header for scope.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioBapBroadcastVendor.h"

#include "esp_ble_audio_lc3_defs.h"
#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_bap_lc3_preset_defs.h"
#include "esp_ble_audio_codec_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_pacs_api.h"
#include "esp_log.h"

#include "audio/BLEAudioVendor.h"  /* engine GAP observer registration */

#include <stdio.h>
#include <string.h>
#include <errno.h>

static const char *BC_TAG = "BLEAudioBapBcast";

/* ── Dispatch into C++ (source direction only) ──────────────────────────── */

static ble_bap_vendor_stream_cbs_t s_cbs;

void bleBapBroadcastVendorSetStreamCbs(const ble_bap_vendor_stream_cbs_t *cbs) {
  if (cbs) {
    s_cbs = *cbs;
  } else {
    memset(&s_cbs, 0, sizeof(s_cbs));
  }
}

/* ── LC3 broadcast preset table ─────────────────────────────────────────── */

ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_16_2_1_DEFINE(s_bc_preset_16_2_1, ESP_BLE_AUDIO_LOCATION_FRONT_LEFT, ESP_BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_24_2_1_DEFINE(s_bc_preset_24_2_1, ESP_BLE_AUDIO_LOCATION_FRONT_LEFT, ESP_BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_4_1_DEFINE(s_bc_preset_48_4_1, ESP_BLE_AUDIO_LOCATION_FRONT_LEFT, ESP_BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED);

static esp_ble_audio_bap_lc3_preset_t *bc_preset_for(ble_bap_vendor_preset_t sel) {
  switch (sel) {
    case BLE_BAP_VENDOR_PRESET_24_2_1: return &s_bc_preset_24_2_1;
    case BLE_BAP_VENDOR_PRESET_48_4_1: return &s_bc_preset_48_4_1;
    case BLE_BAP_VENDOR_PRESET_16_2_1:
    default: return &s_bc_preset_16_2_1;
  }
}

/* ── Source state (one subgroup, one mono stream) ───────────────────────── */

static esp_ble_audio_bap_broadcast_source_t *s_source;
static esp_ble_audio_bap_stream_t s_stream;
static bool s_streaming;

/* Front-left channel allocation LTV for the single stream (location as LE32). */
static uint8_t s_chan_alloc[] = {
  ESP_BLE_AUDIO_CODEC_DATA(ESP_BLE_AUDIO_CODEC_CFG_CHAN_ALLOC, 0x01, 0x00, 0x00, 0x00)
};

/* ── Stream ops (transmit-only) ─────────────────────────────────────────── */

static void bc_stream_started(esp_ble_audio_bap_stream_t *stream) {
  (void)stream;
  s_streaming = true;
  if (s_cbs.started) {
    s_cbs.started(BLE_BAP_VENDOR_DIR_SOURCE);
  }
}

static void bc_stream_stopped(esp_ble_audio_bap_stream_t *stream, uint8_t reason) {
  (void)stream;
  s_streaming = false;
  if (s_cbs.stopped) {
    s_cbs.stopped(BLE_BAP_VENDOR_DIR_SOURCE, reason);
  }
}

static void bc_stream_sent(esp_ble_audio_bap_stream_t *stream, void *user_data) {
  (void)stream;
  (void)user_data;
  if (s_cbs.sent) {
    s_cbs.sent(BLE_BAP_VENDOR_DIR_SOURCE);
  }
}

static esp_ble_audio_bap_stream_ops_t s_bc_ops = {
  .started = bc_stream_started,
  .stopped = bc_stream_stopped,
  .sent = bc_stream_sent,
};

/* ── Source-level callbacks ─────────────────────────────────────────────── */

static void bc_source_started(esp_ble_audio_bap_broadcast_source_t *source) {
  (void)source;
  ESP_LOGI(BC_TAG, "broadcast source started");
}

static void bc_source_stopped(esp_ble_audio_bap_broadcast_source_t *source, uint8_t reason) {
  (void)source;
  ESP_LOGI(BC_TAG, "broadcast source stopped, reason 0x%02x", reason);
}

static esp_ble_audio_bap_broadcast_source_cb_t s_bc_source_cb = {
  .started = bc_source_started,
  .stopped = bc_source_stopped,
};

/* ── API ────────────────────────────────────────────────────────────────── */

int bleBapBroadcastSourceCreate(ble_bap_vendor_preset_t preset, bool encrypt, const uint8_t *code, uint8_t code_len) {
  esp_err_t err;
  esp_ble_audio_bap_lc3_preset_t *p = bc_preset_for(preset);

  esp_ble_audio_bap_broadcast_source_stream_param_t stream_param = {0};
  esp_ble_audio_bap_broadcast_source_subgroup_param_t subgroup_param = {0};
  esp_ble_audio_bap_broadcast_source_param_t create_param = {0};

  err = esp_ble_audio_bap_broadcast_source_register_cb(&s_bc_source_cb);
  if (err) {
    ESP_LOGE(BC_TAG, "source_register_cb: %d", err);
    return (int)err;
  }

  esp_ble_audio_bap_stream_cb_register(&s_stream, &s_bc_ops);

  stream_param.stream = &s_stream;
  stream_param.data = s_chan_alloc;
  stream_param.data_len = sizeof(s_chan_alloc);

  subgroup_param.params_count = 1;
  subgroup_param.params = &stream_param;
  subgroup_param.codec_cfg = &p->codec_cfg;

  create_param.params_count = 1;
  create_param.params = &subgroup_param;
  create_param.qos = &p->qos;
  create_param.packing = ESP_BLE_ISO_PACKING_SEQUENTIAL;
  create_param.encryption = encrypt;
  if (encrypt && code && code_len) {
    memcpy(create_param.broadcast_code, code, code_len > ESP_BLE_ISO_BROADCAST_CODE_SIZE ? ESP_BLE_ISO_BROADCAST_CODE_SIZE : code_len);
  }

  err = esp_ble_audio_bap_broadcast_source_create(&create_param, &s_source);
  if (err) {
    ESP_LOGE(BC_TAG, "source_create: %d", err);
    return (int)err;
  }
  s_streaming = false;
  ESP_LOGI(BC_TAG, "broadcast source created");
  return 0;
}

int bleBapBroadcastSourceGetBase(uint8_t *out, uint16_t cap, uint16_t *out_len) {
  NET_BUF_SIMPLE_DEFINE(base_buf, 128);
  esp_err_t err;

  if (!s_source || !out || !out_len) {
    return -EINVAL;
  }
  err = esp_ble_audio_bap_broadcast_source_get_base(s_source, &base_buf);
  if (err) {
    ESP_LOGE(BC_TAG, "get_base: %d", err);
    return (int)err;
  }
  if (base_buf.len > cap) {
    ESP_LOGE(BC_TAG, "BASE too large (%u > %u)", base_buf.len, cap);
    return -ENOMEM;
  }
  memcpy(out, base_buf.data, base_buf.len);
  *out_len = base_buf.len;
  return 0;
}

int bleBapBroadcastSourceStart(uint8_t adv_handle) {
  esp_err_t err;
  esp_ble_audio_bap_broadcast_adv_info_t info = {
    .adv_handle = adv_handle,
  };

  if (!s_source) {
    return -EINVAL;
  }
  err = esp_ble_audio_bap_broadcast_adv_add(&info);
  if (err) {
    ESP_LOGE(BC_TAG, "adv_add: %d", err);
    return (int)err;
  }
  err = esp_ble_audio_bap_broadcast_source_start(s_source, adv_handle);
  if (err) {
    ESP_LOGE(BC_TAG, "source_start: %d", err);
    return (int)err;
  }
  return 0;
}

int bleBapBroadcastSourceStop(void) {
  if (!s_source) {
    return -EINVAL;
  }
  return (int)esp_ble_audio_bap_broadcast_source_stop(s_source);
}

void bleBapBroadcastSourceDelete(void) {
  if (s_source) {
    esp_ble_audio_bap_broadcast_source_delete(s_source);
    s_source = NULL;
  }
  memset(&s_stream, 0, sizeof(s_stream));
  s_streaming = false;
}

int bleBapBroadcastStreamSend(const uint8_t *sdu, uint16_t len, uint16_t seq_num) {
  if (!s_source) {
    return -EINVAL;
  }
  return (int)esp_ble_audio_bap_stream_send(&s_stream, sdu, len, seq_num);
}

bool bleBapBroadcastStreamIsStreaming(void) {
  return s_streaming;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Broadcast Sink (Auracast receiver) + Scan Delegator (BASS)
 *
 * Single-subgroup, single-stream (mono) receiver, mirroring the source scope.
 * The C++/scan layer drives extended scanning + PA-sync creation (via BLEScan);
 * this TU owns the audio-side lifecycle: it decodes BASE (bis indexes), syncs
 * the BIG on `syncable`, and drives create/delete from the engine GAP events
 * (PA_SYNC / PA_SYNC_LOST) forwarded through bleAudioVendorSetGapObserver.
 * ═════════════════════════════════════════════════════════════════════════ */

#define BC_SINK_PA_HANDLE_INIT 0xFFFF

/* Fallback if the Scan Delegator Kconfig default is not surfaced by the
 * lib-builder defconfig (mirrors the ASE-count fallbacks in the unicast TU). */
#ifndef CONFIG_BT_BAP_BASS_MAX_SUBGROUPS
#define CONFIG_BT_BAP_BASS_MAX_SUBGROUPS 1
#endif

static esp_ble_audio_bap_broadcast_sink_t *s_sink;
static esp_ble_audio_bap_stream_t s_sink_stream;
static esp_ble_audio_bap_stream_t *s_sink_streams_p[1];

static uint16_t s_sink_sync_handle = BC_SINK_PA_HANDLE_INIT;
static uint16_t s_past_conn_handle = 0xFFFF;
static bool s_sink_base_received;
static bool s_sink_syncable_seen;
static bool s_sink_big_syncing;
static uint8_t s_sink_sync_tries;
static bool s_sink_sync_defer;
static uint32_t s_sink_bis_bitfield;
static uint32_t s_sink_requested_bis = ESP_BLE_AUDIO_BAP_BIS_SYNC_NO_PREF;
static uint8_t s_sink_code[ESP_BLE_ISO_BROADCAST_CODE_SIZE];
static bool s_sink_encrypt;
static uint32_t s_sink_target_broadcast_id;
static bool s_sink_streaming;
static ble_bap_bcast_pa_sync_req_fn s_pa_sync_req_fn;

/* PACS sink capability: match the CAP acceptor (phone Auracast). The stack only
 * stores BIS indexes for subgroups whose codec ID is in this list; a missing or
 * mono-only PAC leaves sink indexes at 0 (BapBsnkBitsInBisIdxesNotPresentInBase). */
#define BC_SINK_CONTEXT (ESP_BLE_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA)
#define BC_SINK_LOCATION (ESP_BLE_AUDIO_LOCATION_FRONT_LEFT | ESP_BLE_AUDIO_LOCATION_FRONT_RIGHT)
static uint8_t s_sink_codec_data[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_DATA(
  ESP_BLE_AUDIO_CODEC_CAP_FREQ_ANY, ESP_BLE_AUDIO_CODEC_CAP_DURATION_7_5 | ESP_BLE_AUDIO_CODEC_CAP_DURATION_10,
  ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1, 2), 30, 155, 2
);
static uint8_t s_sink_codec_meta[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_META(BC_SINK_CONTEXT);
static const esp_ble_audio_codec_cap_t s_sink_codec_cap = ESP_BLE_AUDIO_CODEC_CAP_LC3(s_sink_codec_data, s_sink_codec_meta);
static esp_ble_audio_pacs_cap_t s_sink_cap = {
  .codec_cap = &s_sink_codec_cap,
};

/* ── Sink stream ops ─────────────────────────────────────────────────────── */

static void bc_sink_started(esp_ble_audio_bap_stream_t *stream) {
  (void)stream;
  s_sink_streaming = true;
  if (s_cbs.started) {
    s_cbs.started(BLE_BAP_VENDOR_DIR_SINK);
  }
}

static void bc_sink_stopped(esp_ble_audio_bap_stream_t *stream, uint8_t reason) {
  (void)stream;
  s_sink_streaming = false;
  if (s_cbs.stopped) {
    s_cbs.stopped(BLE_BAP_VENDOR_DIR_SINK, reason);
  }
  if (s_sink) {
    esp_ble_audio_bap_broadcast_sink_delete(s_sink);
    s_sink = NULL;
  }
}

static void bc_sink_recv(esp_ble_audio_bap_stream_t *stream, const esp_ble_iso_recv_info_t *info, const uint8_t *data, uint16_t len) {
  (void)stream;
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
  s_cbs.recv(BLE_BAP_VENDOR_DIR_SINK, &out, data, len);
}

static esp_ble_audio_bap_stream_ops_t s_sink_ops = {
  .started = bc_sink_started,
  .stopped = bc_sink_stopped,
  .recv = bc_sink_recv,
};

/* ── Broadcast sink callbacks (BASE + syncable) ──────────────────────────── */

static int bc_sink_create_for_pa(void) {
  esp_err_t err;

  if (s_sink_sync_handle == BC_SINK_PA_HANDLE_INIT) {
    return -EINVAL;
  }
  if (s_sink) {
    (void)esp_ble_audio_bap_broadcast_sink_delete(s_sink);
    s_sink = NULL;
  }
  s_sink_base_received = false;
  s_sink_syncable_seen = false;
  s_sink_big_syncing = false;
  s_sink_sync_tries = 0;
  s_sink_sync_defer = false;
  s_sink_bis_bitfield = 0;
  err = esp_ble_audio_bap_broadcast_sink_create(s_sink_sync_handle, s_sink_target_broadcast_id, &s_sink);
  if (err) {
    ESP_LOGE(BC_TAG, "sink: create failed, err %d id=0x%06x", err, (unsigned)s_sink_target_broadcast_id);
    return (int)err;
  }
  ESP_LOGE(BC_TAG, "sink: created handle %u id=0x%06x", s_sink_sync_handle, (unsigned)s_sink_target_broadcast_id);
  return 0;
}

static bool bc_sink_log_subgroup(const esp_ble_audio_bap_base_subgroup_t *subgroup, void *user_data) {
  esp_ble_audio_bap_base_codec_id_t *out = (esp_ble_audio_bap_base_codec_id_t *)user_data;
  esp_ble_audio_bap_base_codec_id_t codec_id = {0};
  uint8_t n_bis = 0;
  uint32_t bis = 0;

  (void)esp_ble_audio_bap_base_get_subgroup_codec_id(subgroup, &codec_id);
  (void)esp_ble_audio_bap_base_get_subgroup_bis_count(subgroup, &n_bis);
  (void)esp_ble_audio_bap_base_subgroup_get_bis_indexes(subgroup, &bis);
  ESP_LOGE(BC_TAG, "sink: subgroup codec=0x%02x cid=0x%04x vid=0x%04x n_bis=%u bis=0x%08x", codec_id.id, codec_id.cid,
           codec_id.vid, n_bis, (unsigned)bis);
  if (out && out->id == 0) {
    *out = codec_id;
  }
  return true;
}

static void bc_sink_try_big_sync(esp_ble_audio_bap_broadcast_sink_t *sink, const esp_ble_iso_biginfo_t *biginfo) {
  uint32_t sync_bits;
  esp_err_t err;

  if (!sink || s_sink_streaming || s_sink_big_syncing || !s_sink_base_received || s_sink_bis_bitfield == 0) {
    return;
  }
  /* First BIGInfo often arrives in the same PA event as BASE, before the sink
   * object has stored indexes (BapBsnkBitsInBisIdxesNotPresentInBase[...][0]). */
  if (s_sink_sync_defer) {
    s_sink_sync_defer = false;
    ESP_LOGE(BC_TAG, "sink: defer BIG sync until next BIGInfo");
    return;
  }
  /* Packaged lib: CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT=1. Two-bit masks
   * (stereo 0x3/0x6) always fail CannotSyncMoreThanStreams. */
  sync_bits = s_sink_bis_bitfield;
  if (s_sink_sync_tries > 0) {
    /* Second try: the other BIS, in case the stack stored BIT(index) not BIT(index-1). */
    sync_bits = (s_sink_bis_bitfield == 0x1u) ? 0x2u : 0x1u;
  }
  if (s_sink_sync_tries >= 2) {
    return;
  }
  s_sink_sync_tries++;
  if (biginfo && biginfo->encryption && !s_sink_encrypt) {
    ESP_LOGW(BC_TAG, "sink: BIG encrypted but no broadcast code set");
  }
  ESP_LOGE(BC_TAG, "sink: BIG sync try %u bits 0x%08x encrypt=%d", (unsigned)s_sink_sync_tries, (unsigned)sync_bits,
           biginfo && biginfo->encryption ? 1 : 0);
  err = esp_ble_audio_bap_broadcast_sink_sync(sink, sync_bits, s_sink_streams_p, s_sink_encrypt ? s_sink_code : NULL);
  if (err) {
    ESP_LOGE(BC_TAG, "sink: sync failed, err %d", err);
    return;
  }
  s_sink_big_syncing = true;
}

static void bc_sink_base_recv(esp_ble_audio_bap_broadcast_sink_t *sink, const esp_ble_audio_bap_base_t *base, size_t base_size) {
  uint32_t base_bits = 0;
  uint8_t subgroup_count = 0;
  esp_ble_audio_bap_base_codec_id_t codec_id = {0};
  const uint8_t *raw = (const uint8_t *)base;
  char hex[80];
  size_t n;
  size_t off = 0;

  if (s_sink_base_received) {
    return;
  }
  n = base_size < 24 ? base_size : 24;
  hex[0] = '\0';
  for (size_t i = 0; i < n && off + 3 < sizeof(hex); i++) {
    int w = snprintf(hex + off, sizeof(hex) - off, "%02x", raw[i]);
    if (w < 0) {
      break;
    }
    off += (size_t)w;
  }
  ESP_LOGE(BC_TAG, "sink: BASE raw[%u] %s", (unsigned)base_size, hex);
  (void)esp_ble_audio_bap_base_get_subgroup_count(base, &subgroup_count);
  (void)esp_ble_audio_bap_base_foreach_subgroup(base, bc_sink_log_subgroup, &codec_id);
  if (esp_ble_audio_bap_base_get_bis_indexes(base, &base_bits) != 0) {
    ESP_LOGE(BC_TAG, "sink: get_bis_indexes failed (base_size=%u subgroups=%u)", (unsigned)base_size, subgroup_count);
    return;
  }
  if (base_bits == 0) {
    ESP_LOGW(BC_TAG, "sink: BASE has 0 BIS indexes (size=%u subgroups=%u), wait", (unsigned)base_size, subgroup_count);
    return;
  }
  /* One registered stream: keep a single BIS. Phones often advertise stereo
   * (BIS 1|2 = bit0|bit1). Isolate the lowest set bit. */
  s_sink_bis_bitfield = base_bits & (~base_bits + 1u);
  /* Keep an assistant BIS_Sync request if it already arrived; otherwise any BIS. */
  if (s_sink_requested_bis == 0) {
    s_sink_requested_bis = ESP_BLE_AUDIO_BAP_BIS_SYNC_NO_PREF;
  }
  s_sink_base_received = true;
  s_sink_sync_defer = true;
  ESP_LOGE(BC_TAG, "sink: BASE bis 0x%08x picked 0x%08x subgroups=%u size=%u", (unsigned)base_bits, (unsigned)s_sink_bis_bitfield,
           subgroup_count, (unsigned)base_size);
  if (sink) {
    s_sink = sink;
  }
  /* Do not sink_sync here: the stack stores BIS indexes after this callback
   * returns. The next BIGInfo/syncable drives the sync. */
}

static void bc_sink_syncable(esp_ble_audio_bap_broadcast_sink_t *sink, const esp_ble_iso_biginfo_t *biginfo) {
  /* BIGInfo can arrive before BASE is stored on the sink object. */
  s_sink_syncable_seen = true;
  if (sink) {
    s_sink = sink;
  }
  if (!s_sink_base_received || s_sink_bis_bitfield == 0) {
    ESP_LOGE(BC_TAG, "sink: syncable but BASE BIS not ready yet");
    return;
  }
  bc_sink_try_big_sync(s_sink, biginfo);
}

static esp_ble_audio_bap_broadcast_sink_cb_t s_sink_cbs = {
  .base_recv = bc_sink_base_recv,
  .syncable = bc_sink_syncable,
};

/* ── Scan Delegator (BASS) callbacks ─────────────────────────────────────── */

static void sd_recv_state_updated(esp_ble_conn_t *conn, const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state) {
  (void)conn;
  ESP_LOGI(BC_TAG, "BASS recv state: pa 0x%02x enc 0x%02x", recv_state->pa_sync_state, recv_state->encrypt_state);
}

static int sd_pa_sync_req(
  esp_ble_conn_t *conn, const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state, bool past_available, uint16_t pa_interval
) {
  (void)pa_interval;
  if (s_sink_sync_handle != BC_SINK_PA_HANDLE_INIT) {
    return -EALREADY;
  }
  if (!s_pa_sync_req_fn) {
    return -ENOTSUP;
  }

  s_sink_target_broadcast_id = recv_state->broadcast_id;
  if (s_pa_sync_req_fn(
        recv_state->addr.type, recv_state->addr.a.val, recv_state->adv_sid, recv_state->broadcast_id, past_available,
        conn ? conn->handle : 0xFFFF
      ) != 0) {
    ESP_LOGE(BC_TAG, "BASS PA sync request failed (past=%d)", past_available ? 1 : 0);
    return -EIO;
  }

  /* Samsung and other assistants push PA sync over the ACL (PAST). Tell them
   * we are waiting for HCI LE Periodic Advertising Sync Transfer. */
  if (past_available) {
    s_past_conn_handle = conn ? conn->handle : 0xFFFF;
    esp_err_t err = esp_ble_audio_bap_scan_delegator_set_pa_state(recv_state->src_id, ESP_BLE_AUDIO_BAP_PA_STATE_INFO_REQ);
    if (err) {
      ESP_LOGE(BC_TAG, "BASS set PA INFO_REQ failed, err %d", err);
      if (s_past_conn_handle != 0xFFFF) {
        (void)s_pa_sync_req_fn(0, NULL, 0, 0, true, s_past_conn_handle); /* cancel: see C++ (past + existing) */
      }
      return -EIO;
    }
    ESP_LOGI(BC_TAG, "BASS waiting for PAST on conn %u src %u", (unsigned)s_past_conn_handle, recv_state->src_id);
  } else {
    ESP_LOGI(BC_TAG, "BASS creating PA sync without PAST");
  }
  return 0;
}

static int sd_pa_sync_term_req(esp_ble_conn_t *conn, const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state) {
  (void)conn;
  (void)recv_state;
  ESP_LOGI(BC_TAG, "BASS PA sync terminate request");
  return 0;
}

static void sd_broadcast_code(
  esp_ble_conn_t *conn, const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
  const uint8_t broadcast_code[ESP_BLE_ISO_BROADCAST_CODE_SIZE]
) {
  (void)conn;
  (void)recv_state;
  memcpy(s_sink_code, broadcast_code, ESP_BLE_ISO_BROADCAST_CODE_SIZE);
  s_sink_encrypt = true;
  ESP_LOGI(BC_TAG, "BASS broadcast code received");
}

static int sd_bis_sync_req(
  esp_ble_conn_t *conn, const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
  const uint32_t bis_sync_req[CONFIG_BT_BAP_BASS_MAX_SUBGROUPS]
) {
  (void)conn;
  if (!bis_sync_req) {
    return -EINVAL;
  }
  uint32_t req = 0;
  for (uint8_t sg = 0; sg < recv_state->num_subgroups && sg < CONFIG_BT_BAP_BASS_MAX_SUBGROUPS; sg++) {
    if (bis_sync_req[sg]) {
      req = bis_sync_req[sg];
      break;
    }
  }
  s_sink_requested_bis = req ? req : ESP_BLE_AUDIO_BAP_BIS_SYNC_NO_PREF;
  /* Assistant asked to stop: tear the sink down. */
  if (s_sink_streaming && req == 0 && s_sink) {
    esp_ble_audio_bap_broadcast_sink_stop(s_sink);
    esp_ble_audio_bap_broadcast_sink_delete(s_sink);
    s_sink = NULL;
  }
  return 0;
}

static esp_ble_audio_bap_scan_delegator_cb_t s_sd_cbs = {
  .recv_state_updated = sd_recv_state_updated,
  .pa_sync_req = sd_pa_sync_req,
  .pa_sync_term_req = sd_pa_sync_term_req,
  .broadcast_code = sd_broadcast_code,
  .bis_sync_req = sd_bis_sync_req,
};

/* ── Engine GAP observer: PA sync established / lost ──────────────────────── */

static void bc_sink_gap_observer(uint8_t type, const void *event) {
  const esp_ble_audio_gap_app_event_t *ev = (const esp_ble_audio_gap_app_event_t *)event;

  switch (type) {
    case ESP_BLE_AUDIO_GAP_EVENT_PA_SYNC:
    case ESP_BLE_AUDIO_GAP_EVENT_PA_SYNC_PAST:
      if (ev->pa_sync.status) {
        ESP_LOGE(BC_TAG, "sink: PA sync failed, status %d (past=%d)", ev->pa_sync.status,
                 type == ESP_BLE_AUDIO_GAP_EVENT_PA_SYNC_PAST ? 1 : 0);
        s_past_conn_handle = 0xFFFF;
        return;
      }
      s_sink_sync_handle = ev->pa_sync.sync_handle;
      s_sink_base_received = false;
      s_sink_syncable_seen = false;
      s_sink_big_syncing = false;
      s_sink_sync_tries = 0;
      s_sink_sync_defer = false;
      s_sink_bis_bitfield = 0;
      ESP_LOGE(BC_TAG, "sink: PA synced handle %u past=%d id=0x%06x, creating sink", s_sink_sync_handle,
               type == ESP_BLE_AUDIO_GAP_EVENT_PA_SYNC_PAST ? 1 : 0, (unsigned)s_sink_target_broadcast_id);
      (void)bc_sink_create_for_pa();
      break;

    case ESP_BLE_AUDIO_GAP_EVENT_PA_SYNC_LOST:
      if (s_sink_sync_handle == ev->pa_sync_lost.sync_handle) {
        ESP_LOGI(BC_TAG, "sink: PA sync lost (handle %u)", s_sink_sync_handle);
        s_sink_sync_handle = BC_SINK_PA_HANDLE_INIT;
        s_sink_base_received = false;
        s_sink_syncable_seen = false;
        s_sink_big_syncing = false;
        s_sink_sync_tries = 0;
        s_sink_sync_defer = false;
        s_sink_bis_bitfield = 0;
        s_sink_streaming = false;
        if (s_sink) {
          esp_ble_audio_bap_broadcast_sink_delete(s_sink);
          s_sink = NULL;
        }
      }
      break;

    default:
      break;
  }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void bleBapBroadcastSinkSetPaSyncReqFn(ble_bap_bcast_pa_sync_req_fn fn) {
  s_pa_sync_req_fn = fn;
}

void bleBapBroadcastSinkSetTarget(uint32_t broadcast_id) {
  s_sink_target_broadcast_id = broadcast_id & 0xFFFFFF;
}

bool bleBapBroadcastSinkStreaming(void) {
  return s_sink_streaming;
}

int bleBapBroadcastSinkInit(ble_bap_vendor_preset_t preset, bool encrypt, const uint8_t *code, uint8_t code_len) {
  esp_err_t err;
  (void)preset;  /* sink cap is 16/24/48 kHz mono; preset kept for API symmetry */

  s_sink_encrypt = encrypt;
  memset(s_sink_code, 0, sizeof(s_sink_code));
  if (encrypt && code && code_len) {
    memcpy(s_sink_code, code, code_len > ESP_BLE_ISO_BROADCAST_CODE_SIZE ? ESP_BLE_ISO_BROADCAST_CODE_SIZE : code_len);
  }

  const esp_ble_audio_pacs_register_param_t pacs_param = {
    .snk_pac = true,
    .snk_loc = true,
  };
  err = esp_ble_audio_pacs_register(&pacs_param);
  if (err) {
    ESP_LOGE(BC_TAG, "sink: pacs_register %d", err);
    return (int)err;
  }

  s_sink_stream.ops = &s_sink_ops;
  s_sink_streams_p[0] = &s_sink_stream;
  (void)esp_ble_audio_bap_stream_cb_register(&s_sink_stream, &s_sink_ops);

  ESP_LOGE(BC_TAG, "sink: PAC id=0x%02x cid=0x%04x vid=0x%04x data_len=%u", s_sink_codec_cap.id, s_sink_codec_cap.cid,
           s_sink_codec_cap.vid, (unsigned)s_sink_codec_cap.data_len);
  err = esp_ble_audio_pacs_cap_register(ESP_BLE_AUDIO_DIR_SINK, &s_sink_cap);
  if (err) {
    ESP_LOGE(BC_TAG, "sink: pacs_cap_register %d", err);
    return (int)err;
  }
  err = esp_ble_audio_pacs_set_location(ESP_BLE_AUDIO_DIR_SINK, BC_SINK_LOCATION);
  if (err) {
    ESP_LOGE(BC_TAG, "sink: pacs_set_location %d", err);
    return (int)err;
  }
  err = esp_ble_audio_pacs_set_supported_contexts(ESP_BLE_AUDIO_DIR_SINK, BC_SINK_CONTEXT);
  if (err) {
    ESP_LOGE(BC_TAG, "sink: pacs_set_supported_contexts %d", err);
    return (int)err;
  }
  err = esp_ble_audio_pacs_set_available_contexts(ESP_BLE_AUDIO_DIR_SINK, BC_SINK_CONTEXT);
  if (err) {
    ESP_LOGE(BC_TAG, "sink: pacs_set_available_contexts %d", err);
    return (int)err;
  }

  err = esp_ble_audio_bap_scan_delegator_register(&s_sd_cbs);
  if (err) {
    ESP_LOGE(BC_TAG, "sink: scan_delegator_register %d", err);
    return (int)err;
  }

  err = esp_ble_audio_bap_broadcast_sink_register_cb(&s_sink_cbs);
  if (err) {
    ESP_LOGE(BC_TAG, "sink: broadcast_sink_register_cb %d", err);
    return (int)err;
  }

  bleAudioVendorSetGapObserver(bc_sink_gap_observer);
  s_sink_sync_handle = BC_SINK_PA_HANDLE_INIT;
  s_past_conn_handle = 0xFFFF;
  s_sink_base_received = false;
  s_sink_syncable_seen = false;
  s_sink_big_syncing = false;
  s_sink_sync_tries = 0;
  s_sink_sync_defer = false;
  s_sink_bis_bitfield = 0;
  s_sink_requested_bis = ESP_BLE_AUDIO_BAP_BIS_SYNC_NO_PREF;
  s_sink_streaming = false;
  ESP_LOGI(BC_TAG, "broadcast sink initialized");
  return 0;
}

void bleBapBroadcastSinkDeinit(void) {
  bleAudioVendorSetGapObserver(NULL);
  if (s_sink) {
    esp_ble_audio_bap_broadcast_sink_stop(s_sink);
    esp_ble_audio_bap_broadcast_sink_delete(s_sink);
    s_sink = NULL;
  }
  memset(&s_sink_stream, 0, sizeof(s_sink_stream));
  s_sink_sync_handle = BC_SINK_PA_HANDLE_INIT;
  s_past_conn_handle = 0xFFFF;
  s_sink_base_received = false;
  s_sink_syncable_seen = false;
  s_sink_big_syncing = false;
  s_sink_sync_tries = 0;
  s_sink_sync_defer = false;
  s_sink_bis_bitfield = 0;
  s_sink_requested_bis = ESP_BLE_AUDIO_BAP_BIS_SYNC_NO_PREF;
  s_sink_streaming = false;
  s_pa_sync_req_fn = NULL;
}

#endif /* BLE_AUDIO_SUPPORTED */
