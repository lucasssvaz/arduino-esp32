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
 * @file BLEAudioStream.cpp
 * @brief Backend-agnostic per-stream handle + BAP vendor dispatch registry.
 *
 * The stream lives in the BAP engine; this handle carries only the direction
 * and the application callbacks. A tiny per-direction registry lets the single
 * set of global BAP vendor callbacks land on the right stream (the boundary
 * drives one sink + one source stream, matching the single-link demo).
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioStream.h"
#include "audio/BLEAudioStreamInternal.h"
#include "audio/BLEAudioBapVendor.h"
#include "audio/BLEAudioBapBroadcastVendor.h"
#include "esp32-hal-log.h"

struct BLEAudioStream::Impl {
  uint8_t dir = BLEAudioStreamInternal::DIR_SINK;
  bool streaming = false;
  BLEAudioStream::StateCallback onStarted;
  BLEAudioStream::StoppedCallback onStopped;
  BLEAudioStream::ReceiveCallback onReceive;
  BLEAudioStream::SentCallback onSent;
  // When set, write() routes SDUs here (broadcast source); otherwise the
  // unicast BAP vendor send is used.
  BLEAudioStreamInternal::SendFn sender;
};

// --------------------------------------------------------------------------
// Dispatch registry (one slot per data-flow direction)
// --------------------------------------------------------------------------

namespace {

std::shared_ptr<BLEAudioStream::Impl> sByDir[2];

void vStarted(uint8_t dir) {
  if (dir > 1 || !sByDir[dir]) {
    return;
  }
  auto impl = sByDir[dir];
  impl->streaming = true;
  if (impl->onStarted) {
    BLEAudioStream h(impl);
    impl->onStarted(h);
  }
}

void vStopped(uint8_t dir, uint8_t reason) {
  if (dir > 1 || !sByDir[dir]) {
    return;
  }
  auto impl = sByDir[dir];
  impl->streaming = false;
  if (impl->onStopped) {
    BLEAudioStream h(impl);
    impl->onStopped(h, reason);
  }
}

void vRecv(uint8_t dir, const ble_bap_vendor_recv_info_t *info, const uint8_t *data, uint16_t len) {
  if (dir > 1 || !sByDir[dir]) {
    return;
  }
  auto impl = sByDir[dir];
  if (!impl->onReceive) {
    return;
  }
  BLEAudioSduInfo sdu;
  if (info) {
    sdu.timestamp = info->timestamp_us;
    sdu.packetSeqNum = info->seq_num;
    sdu.packetStatus = info->valid ? 0 : 2;
  }
  BLEAudioStream h(impl);
  impl->onReceive(h, sdu, data, len);
}

void vSent(uint8_t dir) {
  if (dir > 1 || !sByDir[dir]) {
    return;
  }
  auto impl = sByDir[dir];
  if (impl->onSent) {
    BLEAudioStream h(impl);
    impl->onSent(h);
  }
}

}  // namespace

namespace BLEAudioStreamInternal {

std::shared_ptr<BLEAudioStream::Impl> makeStream(uint8_t dir) {
  auto impl = std::make_shared<BLEAudioStream::Impl>();
  impl->dir = dir;
  return impl;
}

void registerForDispatch(const std::shared_ptr<BLEAudioStream::Impl> &impl) {
  if (impl && impl->dir <= 1) {
    sByDir[impl->dir] = impl;
  }
}

void clearDispatch() {
  sByDir[0].reset();
  sByDir[1].reset();
}

void setSender(const std::shared_ptr<BLEAudioStream::Impl> &impl, SendFn sender) {
  if (impl) {
    impl->sender = std::move(sender);
  }
}

void installVendorStreamCbs() {
  ble_bap_vendor_stream_cbs_t cbs = {};
  cbs.started = vStarted;
  cbs.stopped = vStopped;
  cbs.recv = vRecv;
  cbs.sent = vSent;
  bleBapVendorSetStreamCbs(&cbs);
}

void installBroadcastVendorStreamCbs() {
  ble_bap_vendor_stream_cbs_t cbs = {};
  cbs.started = vStarted;
  cbs.stopped = vStopped;
  cbs.recv = vRecv;
  cbs.sent = vSent;
  bleBapBroadcastVendorSetStreamCbs(&cbs);
}

}  // namespace BLEAudioStreamInternal

// --------------------------------------------------------------------------
// BLEAudioStream handle
// --------------------------------------------------------------------------

BLEAudioStream::BLEAudioStream() : _impl(nullptr) {}
BLEAudioStream::BLEAudioStream(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}

BLEAudioStream::operator bool() const {
  return _impl != nullptr;
}

bool BLEAudioStream::isSource() const {
  return _impl && _impl->dir == BLEAudioStreamInternal::DIR_SOURCE;
}

bool BLEAudioStream::isStreaming() const {
  return _impl && _impl->streaming;
}

BTStatus BLEAudioStream::write(const uint8_t *sdu, uint16_t len, uint16_t seqNum) {
  if (!_impl) {
    return BTStatus::InvalidState;
  }
  if (_impl->dir != BLEAudioStreamInternal::DIR_SOURCE) {
    log_e("BLEAudioStream::write on a sink stream");
    return BTStatus::InvalidState;
  }
  int err = _impl->sender ? _impl->sender(sdu, len, seqNum) : bleBapVendorStreamSend(BLE_BAP_VENDOR_DIR_SOURCE, sdu, len, seqNum);
  return err == 0 ? BTStatus::OK : BTStatus::Fail;
}

void BLEAudioStream::onStarted(StateCallback cb) {
  if (_impl) {
    _impl->onStarted = std::move(cb);
  }
}
void BLEAudioStream::onStopped(StoppedCallback cb) {
  if (_impl) {
    _impl->onStopped = std::move(cb);
  }
}
void BLEAudioStream::onReceive(ReceiveCallback cb) {
  if (_impl) {
    _impl->onReceive = std::move(cb);
  }
}
void BLEAudioStream::onSent(SentCallback cb) {
  if (_impl) {
    _impl->onSent = std::move(cb);
  }
}

#endif /* BLE_AUDIO_SUPPORTED */
