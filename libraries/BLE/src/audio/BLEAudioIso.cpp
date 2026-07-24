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
 * @file BLEAudioIso.cpp
 * @brief Audio-agnostic ISO transport over the ESP-BLE-ISO engine.
 *
 * Fully shared across NimBLE and Bluedroid: names no stack concept. The vendor
 * Zephyr headers do not parse as C++, so the real `esp_ble_iso_*` calls live in
 * `BLEAudioIsoVendor.c`; this file reaches them through the C-safe
 * `BLEAudioIsoVendor.h` surface and maps the raw `esp_err_t` into `BTStatus`.
 * The pool-owned `Channel` objects are indexed by the same slot the C boundary
 * uses, so the vendor's chan-ops dispatch lands on the right handle.
 */

#include "core/BLEGuards.h"
#if BLE_ISO_SUPPORTED

#include "audio/BLEAudioIso.h"
#include "audio/BLEAudioIsoVendor.h"
#include "esp32-hal-log.h"

namespace BLEAudioIso {

namespace {

Channel sChannels[MAX_CHANNELS];
bool sBegun = false;

Channel *channelForSlot(int slot) {
  if (slot < 0 || slot >= MAX_CHANNELS) {
    return nullptr;
  }
  return &sChannels[slot];
}

// Vendor dispatch trampolines (host task) -> pool Channel -> std::function.
void onVendorConnected(int slot) {
  if (Channel *c = channelForSlot(slot)) {
    c->_dispatchConnected();
  }
}
void onVendorDisconnected(int slot, uint8_t reason) {
  if (Channel *c = channelForSlot(slot)) {
    c->_dispatchDisconnected(reason);
  }
}
void onVendorRecv(int slot, const ble_iso_vendor_recv_info_t *info, const uint8_t *data, uint16_t len) {
  Channel *c = channelForSlot(slot);
  if (!c) {
    return;
  }
  SduInfo sdu;
  if (info) {
    sdu.timestampUs = info->timestamp_us;
    sdu.seqNum = info->seq_num;
    sdu.valid = info->valid;
    sdu.timestampValid = info->ts_valid;
  }
  c->_dispatchReceive(sdu, data, len);
}
void onVendorSent(int slot) {
  if (Channel *c = channelForSlot(slot)) {
    c->_dispatchSent();
  }
}

uint8_t phySel(Phy phy) {
  switch (phy) {
    case Phy::Phy1M: return BLE_ISO_VENDOR_PHY_1M;
    case Phy::PhyCoded: return BLE_ISO_VENDOR_PHY_CODED;
    case Phy::Phy2M:
    default: return BLE_ISO_VENDOR_PHY_2M;
  }
}

}  // namespace

/* ── Channel ────────────────────────────────────────────────────────────── */

bool Channel::isConnected() const {
  return _slot >= 0 && bleIsoVendorIsConnected(_slot);
}

BTStatus Channel::send(const uint8_t *sdu, uint16_t len, uint16_t seqNum) {
  if (_slot < 0) {
    return BTStatus::InvalidState;
  }
  int err = bleIsoVendorSend(_slot, sdu, len, seqNum);
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

void Channel::_dispatchConnected() {
  if (_onConnected) {
    _onConnected(*this);
  }
}
void Channel::_dispatchDisconnected(uint8_t reason) {
  if (_onDisconnected) {
    _onDisconnected(*this, reason);
  }
}
void Channel::_dispatchReceive(const SduInfo &info, const uint8_t *sdu, uint16_t len) {
  if (_onReceive) {
    _onReceive(*this, info, sdu, len);
  }
}
void Channel::_dispatchSent() {
  if (_onSent) {
    _onSent(*this);
  }
}
void Channel::_reset() {
  _slot = -1;
  _onConnected = nullptr;
  _onDisconnected = nullptr;
  _onReceive = nullptr;
  _onSent = nullptr;
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

BTStatus begin() {
  if (sBegun) {
    return BTStatus::OK;
  }
  bleIsoVendorSetCallbacks(onVendorConnected, onVendorDisconnected, onVendorRecv, onVendorSent);
  int err = bleIsoVendorInit();
  if (err != 0) {
    log_e("esp_ble_iso_common_init failed: %d", err);
    return BTStatus::Fail;
  }
  for (auto &c : sChannels) {
    c._reset();
  }
  sBegun = true;
  log_i("BLEAudioIso: transport up");
  return BTStatus::OK;
}

void end() {
  if (!sBegun) {
    return;
  }
  bleIsoVendorDeinit();
  for (auto &c : sChannels) {
    c._reset();
  }
  sBegun = false;
}

bool isActive() {
  return sBegun && bleIsoVendorIsActive();
}

/* ── Factories ──────────────────────────────────────────────────────────── */

namespace {
Channel *allocChannel() {
  int slot = bleIsoVendorAllocSlot();
  if (slot < 0) {
    log_w("BLEAudioIso: channel pool exhausted");
    return nullptr;
  }
  Channel *c = channelForSlot(slot);
  if (!c) {
    bleIsoVendorReleaseSlot(slot);
    return nullptr;
  }
  c->_reset();
  c->_bind(slot);
  return c;
}
}  // namespace

Channel *connectCis(uint16_t connHandle, const CisParams &params) {
  if (!isActive()) {
    log_e("BLEAudioIso: connectCis before begin()");
    return nullptr;
  }
  Channel *c = allocChannel();
  if (!c) {
    return nullptr;
  }
  int err = bleIsoVendorCisConnect(
    c->slot(), connHandle, params.sduSize, phySel(params.phy), params.rtn, params.sduIntervalUs, params.latencyMs, params.packing, params.framing
  );
  if (err != 0) {
    log_e("BLEAudioIso: CIS connect failed: %d", err);
    bleIsoVendorReleaseSlot(c->slot());
    c->_reset();
    return nullptr;
  }
  return c;
}

Channel *listenCis(const CisParams &params) {
  if (!isActive()) {
    log_e("BLEAudioIso: listenCis before begin()");
    return nullptr;
  }
  Channel *c = allocChannel();
  if (!c) {
    return nullptr;
  }
  int err = bleIsoVendorCisListen(c->slot(), params.sduSize);
  if (err != 0) {
    log_e("BLEAudioIso: CIS listen failed: %d", err);
    bleIsoVendorReleaseSlot(c->slot());
    c->_reset();
    return nullptr;
  }
  return c;
}

Channel *createBig(uint8_t advHandle, const BigParams &params) {
  if (!isActive()) {
    log_e("BLEAudioIso: createBig before begin()");
    return nullptr;
  }
  Channel *c = allocChannel();
  if (!c) {
    return nullptr;
  }
  int err = bleIsoVendorBigCreate(
    c->slot(), advHandle, params.sduSize, phySel(params.phy), params.rtn, params.sduIntervalUs, params.latencyMs, params.packing, params.framing,
    params.broadcastCode, params.broadcastCodeLen
  );
  if (err != 0) {
    log_e("BLEAudioIso: BIG create failed: %d", err);
    bleIsoVendorReleaseSlot(c->slot());
    c->_reset();
    return nullptr;
  }
  return c;
}

Channel *syncBig(uint8_t bisIndex, const BigParams &params) {
  if (!isActive()) {
    log_e("BLEAudioIso: syncBig before begin()");
    return nullptr;
  }
  Channel *c = allocChannel();
  if (!c) {
    return nullptr;
  }
  int err = bleIsoVendorBigSyncArm(c->slot(), bisIndex, params.sduSize, /*sync_timeout=*/100, params.broadcastCode, params.broadcastCodeLen);
  if (err != 0) {
    log_e("BLEAudioIso: BIG sync arm failed: %d", err);
    bleIsoVendorReleaseSlot(c->slot());
    c->_reset();
    return nullptr;
  }
  return c;
}

}  // namespace BLEAudioIso

#endif /* BLE_ISO_SUPPORTED */
