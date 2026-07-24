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
 * @file BLEAudioIsoVendor.c
 * @brief C language boundary for ESP-BLE-ISO transport calls.
 *
 * Compiled as C so the vendor public header (and the Zephyr bluetooth headers
 * it pulls in) parse correctly. This is the ONLY translation unit that names
 * `esp_ble_iso_*` types; everything above it goes through the C-safe
 * `BLEAudioIsoVendor.h` surface. It owns a fixed pool of ISO channels and maps
 * the vendor's chan-ops / gap callbacks onto the library's slot-indexed
 * dispatch. See BLEAudioIsoVendor.h for the design rationale.
 */

#include "core/BLEGuards.h"
#if BLE_ISO_SUPPORTED

#include "audio/BLEAudioIsoVendor.h"

#include "esp_ble_iso_common_api.h"
#include "esp_log.h"

#include <string.h>

static const char *BLE_ISO_VENDOR_TAG = "BLEAudioIso";

/* ── Channel pool ───────────────────────────────────────────────────────── */

typedef struct {
  bool used;
  bool has_tx;             /* TX QoS configured (Central CIS / Broadcaster BIS). */
  bool has_rx;             /* RX QoS configured (Peripheral CIS / Sync-receiver BIS). */
  bool listening;          /* Awaiting an inbound CIS via the ISO server accept(). */
  bool remove_path_on_disc;/* Central CIS keeps its data path across disconnect. */
  esp_ble_iso_chan_t chan;
  esp_ble_iso_chan_ops_t ops;
  esp_ble_iso_chan_qos_t qos;
  esp_ble_iso_chan_io_qos_t tx_qos;
  esp_ble_iso_chan_io_qos_t rx_qos;
} iso_slot_t;

static iso_slot_t s_slots[BLE_ISO_VENDOR_MAX_CHAN];
static bool s_active;

static esp_ble_iso_cig_t *s_cig;
static esp_ble_iso_big_t *s_big;

/* BIG-sync arm state: the actual sync is issued when a BIGInfo report arrives. */
static bool s_big_sync_armed;
static bool s_big_synced;
static int s_big_sync_slot = -1;
static uint32_t s_big_sync_bitfield;
static uint16_t s_big_sync_timeout;
static bool s_big_sync_encrypted;
static uint8_t s_big_sync_bcode[BLE_ISO_VENDOR_BCODE_SIZE];

/* Dispatch into C++ (set once before begin()). */
static ble_iso_vendor_connected_fn s_cb_connected;
static ble_iso_vendor_disconnected_fn s_cb_disconnected;
static ble_iso_vendor_recv_fn s_cb_recv;
static ble_iso_vendor_sent_fn s_cb_sent;

/* ── Helpers ────────────────────────────────────────────────────────────── */

static uint8_t map_phy(uint8_t sel) {
  switch (sel) {
    case BLE_ISO_VENDOR_PHY_1M: return ESP_BLE_ISO_PHY_1M;
    case BLE_ISO_VENDOR_PHY_CODED: return ESP_BLE_ISO_PHY_CODED;
    case BLE_ISO_VENDOR_PHY_2M:
    default: return ESP_BLE_ISO_PHY_2M;
  }
}

static int slot_of(const esp_ble_iso_chan_t *chan) {
  for (int i = 0; i < BLE_ISO_VENDOR_MAX_CHAN; i++) {
    if (s_slots[i].used && &s_slots[i].chan == chan) {
      return i;
    }
  }
  return -1;
}

static int alloc_slot(void) {
  for (int i = 0; i < BLE_ISO_VENDOR_MAX_CHAN; i++) {
    if (!s_slots[i].used) {
      memset(&s_slots[i], 0, sizeof(s_slots[i]));
      s_slots[i].used = true;
      s_slots[i].chan.ops = &s_slots[i].ops;
      s_slots[i].chan.qos = &s_slots[i].qos;
      return i;
    }
  }
  return -1;
}

/* ── Vendor chan ops (host task) ────────────────────────────────────────── */

static void iso_connected_cb(esp_ble_iso_chan_t *chan) {
  int slot = slot_of(chan);
  if (slot < 0) {
    return;
  }
  const esp_ble_iso_chan_path_t data_path = {
    .pid = ESP_BLE_ISO_DATA_PATH_HCI,
    .format = ESP_BLE_ISO_CODING_FORMAT_TRANSPARENT,
  };
  /* Auto set up the HCI data path, matching every esp_ble_iso example, so the
   * C++ layer only deals in connected/recv/sent library events. */
  if (s_slots[slot].has_tx) {
    esp_err_t e = esp_ble_iso_setup_data_path(chan, ESP_BLE_ISO_DATA_PATH_DIR_INPUT, &data_path);
    if (e) {
      ESP_LOGE(BLE_ISO_VENDOR_TAG, "[slot %d] setup input path err %d", slot, e);
    }
  }
  if (s_slots[slot].has_rx) {
    esp_err_t e = esp_ble_iso_setup_data_path(chan, ESP_BLE_ISO_DATA_PATH_DIR_OUTPUT, &data_path);
    if (e) {
      ESP_LOGE(BLE_ISO_VENDOR_TAG, "[slot %d] setup output path err %d", slot, e);
    }
  }
  if (s_cb_connected) {
    s_cb_connected(slot);
  }
}

static void iso_disconnected_cb(esp_ble_iso_chan_t *chan, uint8_t reason) {
  int slot = slot_of(chan);
  if (slot < 0) {
    return;
  }
  /* Per BT Core 6.0 §7.7.5 a Central CIS retains its handle + data path across
   * HCI_Disconnection_Complete, so it must be removed explicitly or the next
   * setup returns Command Disallowed. Peripheral CIS / BIG paths are torn down
   * by the controller. */
  if (s_slots[slot].remove_path_on_disc) {
    if (s_slots[slot].has_tx) {
      esp_ble_iso_remove_data_path(chan, ESP_BLE_ISO_DATA_PATH_DIR_INPUT);
    }
    if (s_slots[slot].has_rx) {
      esp_ble_iso_remove_data_path(chan, ESP_BLE_ISO_DATA_PATH_DIR_OUTPUT);
    }
  }
  if (s_cb_disconnected) {
    s_cb_disconnected(slot, reason);
  }
}

static void iso_recv_cb(esp_ble_iso_chan_t *chan, const esp_ble_iso_recv_info_t *info, const uint8_t *data, uint16_t len) {
  int slot = slot_of(chan);
  if (slot < 0 || !s_cb_recv) {
    return;
  }
  ble_iso_vendor_recv_info_t out = {0};
  if (info) {
    out.timestamp_us = info->ts;
    out.seq_num = info->seq_num;
    out.valid = (info->flags & ESP_BLE_ISO_FLAGS_VALID) != 0;
    out.ts_valid = (info->flags & ESP_BLE_ISO_FLAGS_TS) != 0;
  }
  s_cb_recv(slot, &out, data, len);
}

static void iso_sent_cb(esp_ble_iso_chan_t *chan, void *user_data) {
  (void)user_data;
  int slot = slot_of(chan);
  if (slot < 0 || !s_cb_sent) {
    return;
  }
  s_cb_sent(slot);
}

static void bind_ops(int slot) {
  s_slots[slot].ops.connected = iso_connected_cb;
  s_slots[slot].ops.disconnected = iso_disconnected_cb;
  s_slots[slot].ops.recv = iso_recv_cb;
  s_slots[slot].ops.sent = iso_sent_cb;
}

/* ── ISO server (Peripheral CIS accept) ─────────────────────────────────── */

static int iso_accept_cb(const esp_ble_iso_accept_info_t *info, esp_ble_iso_chan_t **chan) {
  (void)info;
  for (int i = 0; i < BLE_ISO_VENDOR_MAX_CHAN; i++) {
    if (s_slots[i].used && s_slots[i].listening && s_slots[i].chan.iso == NULL) {
      *chan = &s_slots[i].chan;
      return 0;
    }
  }
  ESP_LOGW(BLE_ISO_VENDOR_TAG, "no listening slot for incoming CIS");
  return -1;
}

static esp_ble_iso_server_t s_iso_server = {
  .accept = iso_accept_cb,
};
static bool s_server_registered;

/* ── BIGInfo-driven sync (Receiver) ─────────────────────────────────────── */

static void do_big_sync(uint16_t sync_handle, uint8_t nse, bool encryption) {
  if (!s_big_sync_armed || s_big_synced || s_big_sync_slot < 0) {
    return;
  }
  esp_ble_iso_chan_t *bis[1] = {&s_slots[s_big_sync_slot].chan};
  esp_ble_iso_big_sync_param_t param = {0};
  param.bis_channels = bis;
  param.num_bis = 1;
  param.bis_bitfield = s_big_sync_bitfield;
  param.mse = nse;
  param.sync_timeout = s_big_sync_timeout;
  /* The BIG's own advertised encryption flag is authoritative; the armed
   * broadcast code is only applied when the group is actually encrypted. */
  param.encryption = encryption;
  if (encryption) {
    memcpy(param.bcode, s_big_sync_bcode, BLE_ISO_VENDOR_BCODE_SIZE);
  }
  esp_err_t e = esp_ble_iso_big_sync(sync_handle, &param, &s_big);
  if (e) {
    ESP_LOGE(BLE_ISO_VENDOR_TAG, "big_sync err %d", e);
    return;
  }
  s_big_synced = true;
}

static void iso_gap_cb(esp_ble_iso_gap_app_event_t *event) {
  if (event == NULL) {
    return;
  }
  switch (event->type) {
    case ESP_BLE_ISO_GAP_EVENT_BIGINFO_RECV:
      do_big_sync(event->biginfo_recv.sync_handle, event->biginfo_recv.nse, event->biginfo_recv.encryption != 0);
      break;
    case ESP_BLE_ISO_GAP_EVENT_PA_SYNC_LOST:
      s_big_synced = false;
      s_big = NULL;
      break;
    default: break;
  }
}

/* ── Public C-safe surface ──────────────────────────────────────────────── */

void bleIsoVendorSetCallbacks(
  ble_iso_vendor_connected_fn connected, ble_iso_vendor_disconnected_fn disconnected, ble_iso_vendor_recv_fn recv, ble_iso_vendor_sent_fn sent
) {
  s_cb_connected = connected;
  s_cb_disconnected = disconnected;
  s_cb_recv = recv;
  s_cb_sent = sent;
}

int bleIsoVendorInit(void) {
  if (s_active) {
    return 0;
  }
  esp_ble_iso_init_info_t info = {0};
  info.gap_cb = iso_gap_cb;
  esp_err_t e = esp_ble_iso_common_init(&info);
  if (e) {
    return (int)e;
  }
  s_active = true;
  return 0;
}

void bleIsoVendorDeinit(void) {
  if (s_server_registered) {
    esp_ble_iso_server_unregister(&s_iso_server);
    s_server_registered = false;
  }
  if (s_big) {
    esp_ble_iso_big_terminate(s_big);
    s_big = NULL;
  }
  if (s_cig) {
    esp_ble_iso_cig_terminate(s_cig);
    s_cig = NULL;
  }
  memset(s_slots, 0, sizeof(s_slots));
  s_big_sync_armed = false;
  s_big_synced = false;
  s_big_sync_slot = -1;
  s_active = false;
}

bool bleIsoVendorIsActive(void) {
  return s_active;
}

bool bleIsoVendorIsConnected(int slot) {
  if (slot < 0 || slot >= BLE_ISO_VENDOR_MAX_CHAN || !s_slots[slot].used) {
    return false;
  }
  return s_slots[slot].chan.iso != NULL;
}

int bleIsoVendorCisConnect(
  int slot, uint16_t conn_handle, uint16_t sdu, uint8_t phy, uint8_t rtn, uint32_t interval_us, uint16_t latency_ms, uint8_t packing, uint8_t framing
) {
  if (slot < 0 || slot >= BLE_ISO_VENDOR_MAX_CHAN || !s_slots[slot].used) {
    return -1;
  }
  iso_slot_t *s = &s_slots[slot];
  s->has_tx = true;
  s->remove_path_on_disc = true;  /* Central retains the path across disconnect. */
  s->tx_qos.sdu = sdu;
  s->tx_qos.phy = map_phy(phy);
  s->tx_qos.rtn = rtn;
  s->qos.tx = &s->tx_qos;
  s->qos.rx = NULL;
  bind_ops(slot);

  esp_ble_iso_chan_t *channels[1] = {&s->chan};
  if (!s_cig) {
    esp_ble_iso_cig_param_t cig = {0};
    cig.cis_channels = channels;
    cig.num_cis = 1;
    cig.sca = ESP_BLE_ISO_SCA_UNKNOWN;
    cig.packing = packing;
    cig.framing = framing;
    cig.c_to_p_latency = latency_ms;
    cig.p_to_c_latency = latency_ms;
    cig.c_to_p_interval = interval_us;
    cig.p_to_c_interval = interval_us;
    esp_err_t e = esp_ble_iso_cig_create(&cig, &s_cig);
    if (e) {
      return (int)e;
    }
  }

  esp_ble_iso_connect_param_t connect_param = {0};
  connect_param.iso_chan = &s->chan;
  return (int)esp_ble_iso_chan_connect(&connect_param, conn_handle, 1);
}

int bleIsoVendorCisListen(int slot, uint16_t sdu) {
  if (slot < 0 || slot >= BLE_ISO_VENDOR_MAX_CHAN || !s_slots[slot].used) {
    return -1;
  }
  iso_slot_t *s = &s_slots[slot];
  s->has_rx = true;
  s->listening = true;
  s->rx_qos.sdu = sdu;
  s->qos.rx = &s->rx_qos;
  s->qos.tx = NULL;
  bind_ops(slot);

  if (!s_server_registered) {
    esp_err_t e = esp_ble_iso_server_register(&s_iso_server);
    if (e) {
      return (int)e;
    }
    s_server_registered = true;
  }
  return 0;
}

int bleIsoVendorBigCreate(
  int slot, uint8_t adv_handle, uint16_t sdu, uint8_t phy, uint8_t rtn, uint32_t interval_us, uint16_t latency_ms, uint8_t packing, uint8_t framing,
  const uint8_t *bcode, uint8_t bcode_len
) {
  if (slot < 0 || slot >= BLE_ISO_VENDOR_MAX_CHAN || !s_slots[slot].used) {
    return -1;
  }
  iso_slot_t *s = &s_slots[slot];
  s->has_tx = true;
  s->tx_qos.sdu = sdu;
  s->tx_qos.phy = map_phy(phy);
  s->tx_qos.rtn = rtn;
  s->qos.tx = &s->tx_qos;
  s->qos.rx = NULL;
  bind_ops(slot);

  esp_ble_iso_ext_adv_info_t adv_info = {0};
  adv_info.adv_handle = adv_handle;
  esp_err_t e = esp_ble_iso_big_ext_adv_add(&adv_info);
  if (e) {
    return (int)e;
  }

  esp_ble_iso_chan_t *bis[1] = {&s->chan};
  esp_ble_iso_big_create_param_t param = {0};
  param.bis_channels = bis;
  param.num_bis = 1;
  param.interval = interval_us;
  param.latency = latency_ms;
  param.packing = packing;
  param.framing = framing;
  param.encryption = (bcode != NULL && bcode_len > 0);
  if (param.encryption) {
    memset(param.bcode, 0, ESP_BLE_ISO_BROADCAST_CODE_SIZE);
    uint8_t n = bcode_len < ESP_BLE_ISO_BROADCAST_CODE_SIZE ? bcode_len : ESP_BLE_ISO_BROADCAST_CODE_SIZE;
    memcpy(param.bcode, bcode, n);
  }
  return (int)esp_ble_iso_big_create(adv_handle, &param, &s_big);
}

int bleIsoVendorBigSyncArm(int slot, uint8_t bis_index, uint16_t sdu, uint16_t sync_timeout, const uint8_t *bcode, uint8_t bcode_len) {
  if (slot < 0 || slot >= BLE_ISO_VENDOR_MAX_CHAN || !s_slots[slot].used) {
    return -1;
  }
  iso_slot_t *s = &s_slots[slot];
  s->has_rx = true;
  s->rx_qos.sdu = sdu;
  s->qos.rx = &s->rx_qos;
  s->qos.tx = NULL;
  bind_ops(slot);

  s_big_sync_slot = slot;
  s_big_sync_bitfield = ESP_BLE_ISO_BIS_INDEX_BIT(bis_index);
  s_big_sync_timeout = sync_timeout;
  s_big_sync_encrypted = (bcode != NULL && bcode_len > 0);
  memset(s_big_sync_bcode, 0, sizeof(s_big_sync_bcode));
  if (s_big_sync_encrypted) {
    uint8_t n = bcode_len < BLE_ISO_VENDOR_BCODE_SIZE ? bcode_len : BLE_ISO_VENDOR_BCODE_SIZE;
    memcpy(s_big_sync_bcode, bcode, n);
  }
  s_big_synced = false;
  s_big_sync_armed = true;
  return 0;
}

int bleIsoVendorSend(int slot, const uint8_t *sdu, uint16_t len, uint16_t seq_num) {
  if (slot < 0 || slot >= BLE_ISO_VENDOR_MAX_CHAN || !s_slots[slot].used) {
    return -1;
  }
  if (s_slots[slot].chan.iso == NULL) {
    return -1;
  }
  return (int)esp_ble_iso_chan_send(&s_slots[slot].chan, sdu, len, seq_num);
}

void bleIsoVendorGapPostEvent(uint8_t type, void *event) {
#if BLE_NIMBLE
  esp_ble_iso_gap_app_post_event(type, event);
#else
  (void)type;
  (void)event;
#endif
}

/* Exposed only so C++ can allocate a slot without knowing the pool internals. */
int bleIsoVendorAllocSlot(void) {
  return alloc_slot();
}

void bleIsoVendorReleaseSlot(int slot) {
  if (slot >= 0 && slot < BLE_ISO_VENDOR_MAX_CHAN) {
    memset(&s_slots[slot], 0, sizeof(s_slots[slot]));
  }
}

#endif /* BLE_ISO_SUPPORTED */
