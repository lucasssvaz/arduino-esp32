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
 * @file BLEAudioBroadcastSink.cpp
 * @brief Backend-agnostic BAP Broadcast Sink role handle.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioBroadcastSink.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioStreamInternal.h"
#include "audio/BLEAudioBapBroadcastVendor.h"
#include "BLE.h"
#include "scan/BLEScan.h"
#include "scan/BLEAdvertisedDevice.h"
#include "types/BLEUUID.h"
#include "esp32-hal-log.h"
#include "Arduino.h"

#include <functional>

// Broadcast Audio Announcement Service (assigned number 0x1852). Its service
// data carries the 24-bit Broadcast ID the sink matches against.
static constexpr uint16_t BROADCAST_AUDIO_ANNOUNCEMENT_UUID = 0x1852;
static constexpr uint16_t PUBLIC_BROADCAST_ANNOUNCEMENT_UUID = 0x1856;

// The self-initiated scanner and the assistant (BASS) flow both create the PA
// sync in C++; the vendor asks for it through this file-scope trampoline (a C
// function pointer cannot capture, so it forwards to the bound std::function).
static std::function<int(uint8_t, const uint8_t *, uint8_t, uint32_t, bool, uint16_t)> s_paSyncReqFn;

static int sinkPaSyncReqTrampoline(uint8_t addrType, const uint8_t *addr, uint8_t sid, uint32_t broadcastId, bool pastAvailable, uint16_t connHandle) {
  if (s_paSyncReqFn) {
    return s_paSyncReqFn(addrType, addr, sid, broadcastId, pastAvailable, connHandle);
  }
  return -1;
}

struct BLEAudioBroadcastSink::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  BLEAudioCodecPreset preset = BLEAudioCodecPreset::LC3_16_2_1;
  String code;  // non-empty enables decryption
  String targetName;
  uint32_t targetBroadcastId = 0;  // 0 = match any
  std::shared_ptr<BLEAudioStream::Impl> sinkStreamImpl;
  bool created = false;
  bool syncing = false;
  uint16_t pastConnHandle = 0xFFFF;
};

static ble_bap_vendor_preset_t mapPreset(BLEAudioCodecPreset p) {
  switch (p) {
    case BLEAudioCodecPreset::LC3_24_2_1: return BLE_BAP_VENDOR_PRESET_24_2_1;
    case BLEAudioCodecPreset::LC3_48_4_1: return BLE_BAP_VENDOR_PRESET_48_4_1;
    default:                              return BLE_BAP_VENDOR_PRESET_16_2_1;
  }
}

// Pull the 24-bit Broadcast ID out of the 0x1852 service data, if present.
static bool parseBroadcastId(const BLEAdvertisedDevice &dev, uint32_t &outId) {
  size_t count = dev.getServiceDataCount();
  for (size_t i = 0; i < count; i++) {
    if (dev.getServiceDataUUID(i).toUint16() != BROADCAST_AUDIO_ANNOUNCEMENT_UUID) {
      continue;
    }
    size_t len = 0;
    const uint8_t *data = dev.getServiceData(i, &len);
    if (data && len >= 3) {
      outId = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);
      return true;
    }
  }
  return false;
}

static bool hasPublicBroadcastAnnouncement(const BLEAdvertisedDevice &dev) {
  size_t count = dev.getServiceDataCount();
  for (size_t i = 0; i < count; i++) {
    if (dev.getServiceDataUUID(i).toUint16() == PUBLIC_BROADCAST_ANNOUNCEMENT_UUID) {
      return true;
    }
  }
  return false;
}

BLEAudioBroadcastSink::BLEAudioBroadcastSink() : _impl(nullptr) {}

BLEAudioBroadcastSink::operator bool() const {
  return _impl != nullptr;
}

BLEAudioBroadcastSink &BLEAudioBroadcastSink::setPreset(BLEAudioCodecPreset preset) {
  if (_impl) {
    _impl->preset = preset;
  }
  return *this;
}

BLEAudioBroadcastSink &BLEAudioBroadcastSink::setBroadcastCode(const String &code) {
  if (_impl) {
    _impl->code = code;
  }
  return *this;
}

BLEAudioBroadcastSink &BLEAudioBroadcastSink::setTargetName(const String &name) {
  if (_impl) {
    _impl->targetName = name;
  }
  return *this;
}

BLEAudioBroadcastSink &BLEAudioBroadcastSink::setTargetBroadcastId(uint32_t broadcastId) {
  if (_impl) {
    _impl->targetBroadcastId = broadcastId & 0xFFFFFF;
  }
  return *this;
}

BLEAudioStream BLEAudioBroadcastSink::sinkStream() const {
  return _impl ? BLEAudioStream(_impl->sinkStreamImpl) : BLEAudioStream();
}

BTStatus BLEAudioBroadcastSink::start() {
  if (!_impl || !_impl->created) {
    log_e("BroadcastSink::start before audio.start()");
    return BTStatus::InvalidState;
  }

  BLEScan scan = BLE.getScan();

  // Assistant (BASS) flow: vendor forwards a remote sync request here.
  // past=true → enable PAST receive (Samsung's connected-assistant path).
  // addr==NULL → cancel PAST receive on connHandle.
  // Do NOT set syncing here: PAST may never arrive, and we still need to
  // self-scan the phone's 0x1852 announcements.
  std::weak_ptr<Impl> weakForReq = _impl;
  s_paSyncReqFn = [weakForReq](uint8_t addrType, const uint8_t *addr, uint8_t sid, uint32_t broadcastId, bool past, uint16_t conn) -> int {
    auto s = weakForReq.lock();
    if (!s) {
      return -1;
    }
    BLEScan sc = BLE.getScan();
    if (!addr) {
      s->pastConnHandle = 0xFFFF;
      return sc.cancelPeriodicSyncReceive(conn) ? 0 : -1;
    }
    if (broadcastId) {
      bleBapBroadcastSinkSetTarget(broadcastId);
    }
    if (past) {
      BTStatus st = sc.receivePeriodicSync(conn, 0, 20000);
      if (!st) {
        log_e("BroadcastSink PAST receive failed: %s", st.toString());
        return -1;
      }
      s->pastConnHandle = conn;
      log_e("BroadcastSink PAST receive armed conn=%u id=0x%06X", (unsigned)conn, (unsigned)broadcastId);
      return 0;
    }
    BTAddress a(addr, (BTAddress::Type)addrType);
    BTStatus st = sc.createPeriodicSync(a, sid, 0, 20000);
    if (!st) {
      log_e("BroadcastSink PA sync create failed: %s", st.toString());
      return -1;
    }
    log_e("BroadcastSink PA sync create sid=%u id=0x%06X", (unsigned)sid, (unsigned)broadcastId);
    return 0;
  };

  // Self-initiated flow: scan and sync to the first matching source.
  // Duplicate filter MUST be off: phones put SyncInfo (PA interval) on ADV_EXT_IND
  // and 0x1852/0x1856 on the AUX. With filtering, the controller often reports
  // only the empty primary and the sink never sees a Broadcast ID.
  // Keep scanning until PA sync is established — stopping first makes
  // createPeriodicSync miss the train. Stay scanning even while a phone is
  // connected: Samsung often does not write BASS Add Source, so PAST never
  // comes and self-scan is the only way to get BASE.
  std::weak_ptr<Impl> weak = _impl;
  scan.setActiveScan(false);
  scan.setFilterDuplicates(false);
  scan.onPeriodicSync([weak](uint16_t handle, uint8_t sid, const BTAddress &addr, BLEPhy, uint16_t interval) {
    auto s = weak.lock();
    if (!s) {
      return;
    }
    s->syncing = true;
    log_e(
      "BroadcastSink PA synced handle=%u sid=%u interval=%u addr=%s", (unsigned)handle, (unsigned)sid, (unsigned)interval, addr.toString().c_str()
    );
    Serial.printf(
      "[DUT] PA synced handle=%u sid=%u interval=%u addr=%s\n", (unsigned)handle, (unsigned)sid, (unsigned)interval, addr.toString().c_str()
    );
    BLE.getScan().stopExtended();
  });
  BLE.createServer().onConnect([weak](BLEServer, const BLEConnInfo &info) {
    auto s = weak.lock();
    if (!s) {
      return;
    }
    // Log only. Do not arm PAST here: Bumble connects first for GATT and a
    // pending PAST receive occupies the controller's single sync-create slot,
    // so createPeriodicSync on the phone's 0x1852 would fail with EALREADY.
    Serial.printf("[DUT] peer connected %s\n", info.getAddress().toString().c_str());
  });
  BLE.createServer().onDisconnect([weak](BLEServer, const BLEConnInfo &, uint8_t) {
    auto s = weak.lock();
    if (!s || s->syncing) {
      return;
    }
    BLEScan sc = BLE.getScan();
    if (sc.isScanning()) {
      return;
    }
    log_e("BroadcastSink: peer disconnected, restarting source scan");
    (void)sc.startExtended(0);
  });
  scan.onResult([weak](const BLEAdvertisedDevice &dev) {
    auto s = weak.lock();
    if (!s || s->syncing) {
      return;
    }
    uint32_t id = 0;
    const bool hasBid = parseBroadcastId(dev, id);
    const bool hasPba = hasPublicBroadcastAnnouncement(dev);
    if (!hasBid && !hasPba) {
      return;
    }
    log_e(
      "BroadcastSink saw name='%s' id=0x%06X pba=%d sid=%u pa=%u", dev.getName().c_str(), (unsigned)id, hasPba ? 1 : 0, (unsigned)dev.getAdvSID(),
      (unsigned)dev.getPeriodicInterval()
    );
    Serial.printf(
      "[DUT] bcast adv name='%s' id=0x%06X pba=%d sid=%u pa=%u\n", dev.getName().c_str(), (unsigned)id, hasPba ? 1 : 0, (unsigned)dev.getAdvSID(),
      (unsigned)dev.getPeriodicInterval()
    );
    // Need the 24-bit Broadcast ID (0x1852). PBA-only reports are noted above
    // but cannot create a sink: sink_create(id=0) never stores BASE.
    if (!hasBid) {
      return;
    }
    if (s->targetName.length() && dev.getName() != s->targetName) {
      return;
    }
    if (s->targetBroadcastId && id != s->targetBroadcastId) {
      return;
    }

    log_e("BroadcastSink matching id=0x%06X, creating PA sync", (unsigned)id);
    Serial.printf("[DUT] matching id=0x%06X, creating PA sync\n", (unsigned)id);
    bleBapBroadcastSinkSetTarget(id);
    BLEScan sc = BLE.getScan();
    const uint16_t past = s->pastConnHandle;
    if (past != 0xFFFF) {
      (void)sc.cancelPeriodicSyncReceive(past);
      s->pastConnHandle = 0xFFFF;
    }
    BTStatus pst = sc.createPeriodicSync(dev.getAddress(), dev.getAdvSID(), 0, 20000);
    if (!pst) {
      log_e("BroadcastSink createPeriodicSync failed: %s", pst.toString());
      if (past != 0xFFFF && sc.receivePeriodicSync(past, 0, 20000)) {
        s->pastConnHandle = past;
      }
      return;
    }
    s->syncing = true;
  });

  BTStatus st = scan.startExtended(0);
  if (!st) {
    log_e("BroadcastSink scan start failed: %s", st.toString());
    _impl->syncing = false;
    return st;
  }
  log_i("BroadcastSink scanning for a source...");
  return BTStatus::OK;
}

BTStatus BLEAudioBroadcastSink::stop() {
  if (!_impl) {
    return BTStatus::InvalidState;
  }
  BLEScan scan = BLE.getScan();
  scan.stopExtended();
  scan.cancelPeriodicSync();  // best-effort: cancels a still-pending sync create
  scan.resetCallbacks();
  _impl->syncing = false;
  return BTStatus::OK;
}

// --------------------------------------------------------------------------
// Factory
// --------------------------------------------------------------------------

BLEAudioBroadcastSink BLEAudio::createBroadcastSink() {
  using namespace BLEAudioStreamInternal;
  if (!_impl) {
    log_e("createBroadcastSink() on null controller handle");
    return BLEAudioBroadcastSink();
  }

  auto sink = std::make_shared<BLEAudioBroadcastSink::Impl>();
  sink->audio = _impl;
  sink->sinkStreamImpl = makeStream(DIR_SINK);

  std::weak_ptr<BLEAudioBroadcastSink::Impl> weak = sink;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    registerForDispatch(s->sinkStreamImpl);
    installBroadcastVendorStreamCbs();
    bleBapBroadcastSinkSetPaSyncReqFn(sinkPaSyncReqTrampoline);
    int err = bleBapBroadcastSinkInit(
      mapPreset(s->preset), s->code.length() > 0, (const uint8_t *)s->code.c_str(), (uint8_t)s->code.length()
    );
    if (err != 0) {
      return BTStatus::Fail;
    }
    s->created = true;
    return BTStatus::OK;
  });

  return BLEAudioBroadcastSink(sink);
}

#endif /* BLE_AUDIO_SUPPORTED */
