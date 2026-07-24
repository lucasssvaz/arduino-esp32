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
 * @file BLEAudioBroadcastSource.cpp
 * @brief Backend-agnostic BAP Broadcast Source role handle.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioBroadcastSource.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioStreamInternal.h"
#include "audio/BLEAudioBapBroadcastVendor.h"
#include "audio/BLEAudioProfiles.h"
#include "BLE.h"
#include "advertising/BLEAdvertising.h"
#include "advertising/BLEAdvertisementData.h"
#include "types/BLEAdvTypes.h"
#include "esp32-hal-log.h"

// Broadcast Audio Announcement Service (assigned number 0x1852). The BASE bytes
// from the vendor already carry the Basic Audio Announcement UUID (0x1851).
static constexpr uint16_t BROADCAST_AUDIO_ANNOUNCEMENT_UUID = 0x1852;
static constexpr uint16_t PUBLIC_BROADCAST_ANNOUNCEMENT_UUID = 0x1856;
static constexpr uint8_t AD_TYPE_SERVICE_DATA_16 = 0x16;
static constexpr uint16_t GAP_APPEARANCE_BROADCASTING_DEVICE = 0x0885;
static constexpr uint8_t META_STREAM_CONTEXT = 0x02;
static constexpr uint8_t META_PROGRAM_INFO = 0x03;
static constexpr uint16_t CONTEXT_MEDIA = 0x0004;

static uint8_t pbaFeaturesForPreset(BLEAudioCodecPreset preset, bool encrypt) {
  uint8_t feat;
  switch (preset) {
    case BLEAudioCodecPreset::LC3_48_1_1:
    case BLEAudioCodecPreset::LC3_48_2_1:
    case BLEAudioCodecPreset::LC3_48_3_1:
    case BLEAudioCodecPreset::LC3_48_4_1:
    case BLEAudioCodecPreset::LC3_48_5_1:
    case BLEAudioCodecPreset::LC3_48_6_1:
      feat = BLEAudioPublicBroadcast::HighQuality;
      break;
    default:
      feat = BLEAudioPublicBroadcast::StandardQuality;
      break;
  }
  if (encrypt) {
    feat |= BLEAudioPublicBroadcast::Encryption;
  }
  return feat;
}

static size_t fillPbaMetadata(const String &name, uint8_t *out, size_t cap) {
  size_t n = name.length();
  if (n > 40) {
    n = 40;
  }
  const size_t need = 4 + 2 + n;
  if (!out || need > cap) {
    return 0;
  }
  size_t o = 0;
  out[o++] = 3;
  out[o++] = META_STREAM_CONTEXT;
  out[o++] = (uint8_t)(CONTEXT_MEDIA & 0xFF);
  out[o++] = (uint8_t)(CONTEXT_MEDIA >> 8);
  out[o++] = (uint8_t)(1 + n);
  out[o++] = META_PROGRAM_INFO;
  memcpy(out + o, name.c_str(), n);
  return o + n;
}

struct BLEAudioBroadcastSource::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  BLEAudioCodecPreset preset = BLEAudioCodecPreset::LC3_16_2_1;
  uint32_t broadcastId = 0x123456;
  String code;  // non-empty enables encryption
  String name;
  std::shared_ptr<BLEAudioStream::Impl> sourceStreamImpl;
  bool created = false;
};

static ble_bap_vendor_preset_t mapPreset(BLEAudioCodecPreset p) {
  switch (p) {
    case BLEAudioCodecPreset::LC3_24_2_1: return BLE_BAP_VENDOR_PRESET_24_2_1;
    case BLEAudioCodecPreset::LC3_48_4_1: return BLE_BAP_VENDOR_PRESET_48_4_1;
    default:                              return BLE_BAP_VENDOR_PRESET_16_2_1;
  }
}

BLEAudioBroadcastSource::BLEAudioBroadcastSource() : _impl(nullptr) {}

BLEAudioBroadcastSource::operator bool() const {
  return _impl != nullptr;
}

BLEAudioBroadcastSource &BLEAudioBroadcastSource::setPreset(BLEAudioCodecPreset preset) {
  if (_impl) {
    _impl->preset = preset;
  }
  return *this;
}

BLEAudioBroadcastSource &BLEAudioBroadcastSource::setBroadcastId(uint32_t broadcastId) {
  if (_impl) {
    _impl->broadcastId = broadcastId & 0xFFFFFF;
  }
  return *this;
}

BLEAudioBroadcastSource &BLEAudioBroadcastSource::setBroadcastCode(const String &code) {
  if (_impl) {
    _impl->code = code;
  }
  return *this;
}

BLEAudioBroadcastSource &BLEAudioBroadcastSource::setName(const String &name) {
  if (_impl) {
    _impl->name = name;
  }
  return *this;
}

BLEAudioStream BLEAudioBroadcastSource::sourceStream() const {
  return _impl ? BLEAudioStream(_impl->sourceStreamImpl) : BLEAudioStream();
}

BTStatus BLEAudioBroadcastSource::start(uint8_t advInstance) {
  if (!_impl || !_impl->created) {
    log_e("BroadcastSource::start before audio.start()");
    return BTStatus::InvalidState;
  }

  uint8_t base[128];
  uint16_t baseLen = 0;
  if (bleBapBroadcastSourceGetBase(base, sizeof(base), &baseLen) != 0) {
    return BTStatus::Fail;
  }

  BLEAdvertising adv = BLE.getAdvertising();
  adv.setExtType(advInstance, BLEAdvType::NonConnectable);
  // Primary + secondary 1M: phone Auracast scanners often fail to join when the
  // AUX/periodic carrier is 2M (they list the source as "Unknown" and ignore taps).
  adv.setExtPhy(advInstance, BLEPhy::PHY_1M, BLEPhy::PHY_1M);
  adv.setExtSID(advInstance, 1);

  String advName = _impl->name.length() ? _impl->name : String(BLE.getDeviceName().c_str());

  // Extended adv: Flags + Appearance + 0x1852 + PBA 0x1856 + Broadcast Name 0x30.
  // Samsung Listen uses Broadcast Name (not Complete Local Name) as the title.
  BLEAdvertisementData extData(BLEAdvertisementData::EXTENDED_MAX_PAYLOAD);
  extData.setFlags(BLEAdvFlag::GeneralDisc | BLEAdvFlag::BrEdrNotSupported);
  extData.setAppearance(GAP_APPEARANCE_BROADCASTING_DEVICE);
  uint8_t id3[3] = {
    (uint8_t)(_impl->broadcastId & 0xFF),
    (uint8_t)((_impl->broadcastId >> 8) & 0xFF),
    (uint8_t)((_impl->broadcastId >> 16) & 0xFF),
  };
  extData.setServiceData(BLEUUID(BROADCAST_AUDIO_ANNOUNCEMENT_UUID), id3, sizeof(id3));
#if BLE_AUDIO_PBP_SUPPORTED
  uint8_t meta[48];
  size_t metaLen = fillPbaMetadata(advName, meta, sizeof(meta));
  uint8_t pba[64];
  uint8_t pbaFeat = pbaFeaturesForPreset(_impl->preset, _impl->code.length() > 0);
  size_t pbaLen = BLEAudioPublicBroadcast::buildAnnouncement(pbaFeat, meta, metaLen, pba, sizeof(pba));
  if (pbaLen > 2) {
    extData.setServiceData(BLEUUID(PUBLIC_BROADCAST_ANNOUNCEMENT_UUID), pba + 2, pbaLen - 2);
  }
#endif
  extData.setBroadcastName(advName);
  extData.setName(advName);
  (void)adv.setExtAdvertisementData(advInstance, extData);

  // Periodic adv: the BASE, wrapped as a Service Data - 16 bit AD structure
  // (the BASE already includes its 0x1851 UUID).
  adv.setPeriodicAdvInterval(advInstance, 0x20, 0x40);
  BLEAdvertisementData perData(BLEAdvertisementData::EXTENDED_MAX_PAYLOAD);
  uint8_t field[2 + sizeof(base)];
  field[0] = (uint8_t)(1 + baseLen);
  field[1] = AD_TYPE_SERVICE_DATA_16;
  memcpy(field + 2, base, baseLen);
  perData.addRaw(field, 2 + baseLen);
  (void)adv.setPeriodicAdvData(advInstance, perData);

  (void)adv.startExtended(advInstance, 0, 0);
  (void)adv.startPeriodicAdv(advInstance);

  int err = bleBapBroadcastSourceStart(advInstance);
  if (err != 0) {
    return BTStatus::Fail;
  }
  log_i("BroadcastSource streaming on adv instance %u (id 0x%06X)", advInstance, (unsigned)_impl->broadcastId);
  return BTStatus::OK;
}

BTStatus BLEAudioBroadcastSource::stop() {
  if (!_impl) {
    return BTStatus::InvalidState;
  }
  int err = bleBapBroadcastSourceStop();
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

// --------------------------------------------------------------------------
// Factory
// --------------------------------------------------------------------------

BLEAudioBroadcastSource BLEAudio::createBroadcastSource() {
  using namespace BLEAudioStreamInternal;
  if (!_impl) {
    log_e("createBroadcastSource() on null controller handle");
    return BLEAudioBroadcastSource();
  }

  auto src = std::make_shared<BLEAudioBroadcastSource::Impl>();
  src->audio = _impl;
  src->sourceStreamImpl = makeStream(DIR_SOURCE);
  setSender(src->sourceStreamImpl, [](const uint8_t *sdu, uint16_t len, uint16_t seq) {
    return bleBapBroadcastStreamSend(sdu, len, seq);
  });

  std::weak_ptr<BLEAudioBroadcastSource::Impl> weak = src;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    registerForDispatch(s->sourceStreamImpl);
    installBroadcastVendorStreamCbs();
    int err = bleBapBroadcastSourceCreate(
      mapPreset(s->preset), s->code.length() > 0, (const uint8_t *)s->code.c_str(), (uint8_t)s->code.length()
    );
    if (err != 0) {
      return BTStatus::Fail;
    }
    s->created = true;
    return BTStatus::OK;
  });

  return BLEAudioBroadcastSource(src);
}

#endif /* BLE_AUDIO_SUPPORTED */
