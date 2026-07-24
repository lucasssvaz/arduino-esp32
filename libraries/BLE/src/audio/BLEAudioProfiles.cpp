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
 * @file BLEAudioProfiles.cpp
 * @brief Backend-agnostic TMAP/GMAP top-level profile identity handles.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioProfiles.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioProfilesVendor.h"
#include "esp32-hal-log.h"

#include <errno.h>

// --------------------------------------------------------------------------
// Role Impls
// --------------------------------------------------------------------------

struct BLEAudioTmap::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  BLEAudioTmapRole roles = BLEAudioTmapRole::None;
  DiscoverCallback onDiscover;
};

struct BLEAudioGmap::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  BLEAudioGmapRole roles = BLEAudioGmapRole::None;
  uint8_t uggFeat = 0;
  uint8_t ugtFeat = 0;
  uint8_t bgsFeat = 0;
  uint8_t bgrFeat = 0;
  DiscoverCallback onDiscover;
};

// --------------------------------------------------------------------------
// Single-link dispatch registry + C-callback trampolines.
// --------------------------------------------------------------------------

namespace {

std::weak_ptr<BLEAudioTmap::Impl> s_tmap;
std::weak_ptr<BLEAudioGmap::Impl> s_gmap;

BTStatus toStatus(int err) {
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

void tmapDiscTramp(int err, uint8_t peerRoles) {
  auto t = s_tmap.lock();
  if (t && t->onDiscover) {
    t->onDiscover(toStatus(err), static_cast<BLEAudioTmapRole>(peerRoles));
  }
}

void gmapDiscTramp(int err, uint8_t peerRoles) {
  auto g = s_gmap.lock();
  if (g && g->onDiscover) {
    g->onDiscover(toStatus(err), static_cast<BLEAudioGmapRole>(peerRoles));
  }
}

}  // namespace

// --------------------------------------------------------------------------
// BLEAudioTmap
// --------------------------------------------------------------------------

BLEAudioTmap::BLEAudioTmap() : _impl(nullptr) {}

BLEAudioTmap::operator bool() const {
  return _impl != nullptr;
}

BLEAudioTmap &BLEAudioTmap::setRoles(BLEAudioTmapRole roles) {
  if (_impl) {
    _impl->roles = roles;
  }
  return *this;
}

BTStatus BLEAudioTmap::discover(uint16_t connHandle) {
  return _impl ? toStatus(bleTmapVendorDiscover(connHandle)) : BTStatus::InvalidState;
}

BLEAudioTmap &BLEAudioTmap::onDiscovered(DiscoverCallback cb) {
  if (_impl) {
    _impl->onDiscover = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// BLEAudioGmap
// --------------------------------------------------------------------------

BLEAudioGmap::BLEAudioGmap() : _impl(nullptr) {}

BLEAudioGmap::operator bool() const {
  return _impl != nullptr;
}

BLEAudioGmap &BLEAudioGmap::setRoles(BLEAudioGmapRole roles) {
  if (_impl) {
    _impl->roles = roles;
  }
  return *this;
}

BLEAudioGmap &BLEAudioGmap::setFeatures(uint8_t unicastGatewayFeat, uint8_t unicastTerminalFeat, uint8_t broadcastSenderFeat, uint8_t broadcastReceiverFeat) {
  if (_impl) {
    _impl->uggFeat = unicastGatewayFeat;
    _impl->ugtFeat = unicastTerminalFeat;
    _impl->bgsFeat = broadcastSenderFeat;
    _impl->bgrFeat = broadcastReceiverFeat;
  }
  return *this;
}

BTStatus BLEAudioGmap::discover(uint16_t connHandle) {
  return _impl ? toStatus(bleGmapVendorDiscover(connHandle)) : BTStatus::InvalidState;
}

BLEAudioGmap &BLEAudioGmap::onDiscovered(DiscoverCallback cb) {
  if (_impl) {
    _impl->onDiscover = std::move(cb);
  }
  return *this;
}

// --------------------------------------------------------------------------
// Factories (BLEAudio::create*)
// --------------------------------------------------------------------------

BLEAudioTmap BLEAudio::createTmap() {
  if (!_impl) {
    log_e("createTmap() on null controller handle");
    return BLEAudioTmap();
  }
  auto t = std::make_shared<BLEAudioTmap::Impl>();
  t->audio = _impl;
  s_tmap = t;
  bleTmapVendorSetDiscCb(tmapDiscTramp);

  std::weak_ptr<BLEAudioTmap::Impl> weak = t;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    // A pure TMAP client (no local roles) needs no TMAS registration.
    if (s->roles == BLEAudioTmapRole::None) {
      return BTStatus::OK;
    }
    int err = bleTmapVendorRegister(static_cast<uint8_t>(s->roles));
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioTmap(t);
}

BLEAudioGmap BLEAudio::createGmap() {
  if (!_impl) {
    log_e("createGmap() on null controller handle");
    return BLEAudioGmap();
  }
  auto g = std::make_shared<BLEAudioGmap::Impl>();
  g->audio = _impl;
  s_gmap = g;
  bleGmapVendorSetDiscCb(gmapDiscTramp);

  std::weak_ptr<BLEAudioGmap::Impl> weak = g;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    // A pure GMAP client (no local roles) needs no GMAS registration.
    if (s->roles == BLEAudioGmapRole::None) {
      return BTStatus::OK;
    }
    // Server publish is stubbed on packaged release/v6.1 libs (no gmas.c).
    int err = bleGmapVendorRegister(static_cast<uint8_t>(s->roles), s->uggFeat, s->ugtFeat, s->bgsFeat, s->bgrFeat);
    if (err == -ENOTSUP) {
      log_w("GMAP setRoles() ignored: GMAS not supported on packaged IDF");
      return BTStatus::OK;
    }
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioGmap(g);
}

// --------------------------------------------------------------------------
// BLEAudioPublicBroadcast (PBP announcement helper)
// --------------------------------------------------------------------------

size_t BLEAudioPublicBroadcast::buildAnnouncement(uint8_t features, const uint8_t *metadata, size_t metadataLen, uint8_t *out, size_t outCap) {
  if (!out || outCap == 0) {
    return 0;
  }
  int n = blePbpVendorBuildAnnouncement(metadata, static_cast<uint16_t>(metadataLen), features, out, static_cast<uint16_t>(outCap));
  return n > 0 ? static_cast<size_t>(n) : 0;
}

bool BLEAudioPublicBroadcast::parseAnnouncement(const uint8_t *data, size_t dataLen, uint8_t &featuresOut, const uint8_t *&metadataOut, size_t &metadataLenOut) {
  uint8_t feat = 0;
  const uint8_t *meta = nullptr;
  uint8_t metaLen = 0;
  int rc = blePbpVendorParseAnnouncement(data, static_cast<uint8_t>(dataLen), &feat, &meta, &metaLen);
  if (rc != 0) {
    return false;
  }
  featuresOut = feat;
  metadataOut = meta;
  metadataLenOut = metaLen;
  return true;
}

#endif /* BLE_AUDIO_SUPPORTED */
