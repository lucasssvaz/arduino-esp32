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
 * @file BLEAudioUnicastServer.cpp
 * @brief Backend-agnostic BAP Unicast Server role handle.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioUnicastServer.h"
#include "audio/BLEAudioImpl.h"
#include "audio/BLEAudioStreamInternal.h"
#include "audio/BLEAudioBapVendor.h"
#include "esp32-hal-log.h"

struct BLEAudioUnicastServer::Impl {
  std::shared_ptr<BLEAudio::Impl> audio;
  bool sink = true;
  bool source = true;
  BLEAudioContext sinkContexts = BLEAudioContext::Unspecified | BLEAudioContext::Conversational | BLEAudioContext::Media;
  BLEAudioContext sourceContexts = BLEAudioContext::Unspecified | BLEAudioContext::Conversational | BLEAudioContext::Media;
  // Mono matches the default LC3_*_1 presets and Linux PipeWire BAP clients.
  BLEAudioLocation sinkLocation = BLEAudioLocation::Mono;
  BLEAudioLocation sourceLocation = BLEAudioLocation::Mono;
  std::shared_ptr<BLEAudioStream::Impl> sinkStreamImpl;
  std::shared_ptr<BLEAudioStream::Impl> sourceStreamImpl;
};

BLEAudioUnicastServer::BLEAudioUnicastServer() : _impl(nullptr) {}

BLEAudioUnicastServer::operator bool() const {
  return _impl != nullptr;
}

BLEAudioUnicastServer &BLEAudioUnicastServer::enableSink(bool on) {
  if (_impl) {
    _impl->sink = on;
  }
  return *this;
}
BLEAudioUnicastServer &BLEAudioUnicastServer::enableSource(bool on) {
  if (_impl) {
    _impl->source = on;
  }
  return *this;
}
BLEAudioUnicastServer &BLEAudioUnicastServer::setSinkContexts(BLEAudioContext contexts) {
  if (_impl) {
    _impl->sinkContexts = contexts;
  }
  return *this;
}
BLEAudioUnicastServer &BLEAudioUnicastServer::setSourceContexts(BLEAudioContext contexts) {
  if (_impl) {
    _impl->sourceContexts = contexts;
  }
  return *this;
}
BLEAudioUnicastServer &BLEAudioUnicastServer::setSinkLocation(BLEAudioLocation location) {
  if (_impl) {
    _impl->sinkLocation = location;
  }
  return *this;
}
BLEAudioUnicastServer &BLEAudioUnicastServer::setSourceLocation(BLEAudioLocation location) {
  if (_impl) {
    _impl->sourceLocation = location;
  }
  return *this;
}

BLEAudioStream BLEAudioUnicastServer::sinkStream() const {
  return _impl ? BLEAudioStream(_impl->sinkStreamImpl) : BLEAudioStream();
}
BLEAudioStream BLEAudioUnicastServer::sourceStream() const {
  return _impl ? BLEAudioStream(_impl->sourceStreamImpl) : BLEAudioStream();
}

// --------------------------------------------------------------------------
// Factory (in BLEAudio.cpp translation unit context via this definition)
// --------------------------------------------------------------------------

BLEAudioUnicastServer BLEAudio::createUnicastServer() {
  using namespace BLEAudioStreamInternal;
  if (!_impl) {
    log_e("createUnicastServer() on null controller handle");
    return BLEAudioUnicastServer();
  }

  auto srv = std::make_shared<BLEAudioUnicastServer::Impl>();
  srv->audio = _impl;
  srv->sinkStreamImpl = makeStream(DIR_SINK);
  srv->sourceStreamImpl = makeStream(DIR_SOURCE);

  // Stage the PACS/ASCS registration to run at controller start(), after
  // common_init and before the single GATT commit.
  std::weak_ptr<BLEAudioUnicastServer::Impl> weak = srv;
  _impl->roleApplies.push_back([weak]() -> BTStatus {
    auto s = weak.lock();
    if (!s) {
      return BTStatus::OK;
    }
    registerForDispatch(s->sinkStreamImpl);
    registerForDispatch(s->sourceStreamImpl);
    installVendorStreamCbs();
    int err = bleBapVendorServerInit(
      s->sink, s->source, static_cast<uint16_t>(s->sinkContexts), static_cast<uint16_t>(s->sourceContexts), static_cast<uint32_t>(s->sinkLocation),
      static_cast<uint32_t>(s->sourceLocation)
    );
    return err == 0 ? BTStatus::OK : BTStatus::Fail;
  });

  return BLEAudioUnicastServer(srv);
}

#endif /* BLE_AUDIO_SUPPORTED */
