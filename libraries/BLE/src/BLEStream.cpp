/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
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

#include "impl/BLEGuards.h"
#if BLE_ENABLED

#include "BLEStream.h"
#include "BLE.h"
#include "BLEAdvertising.h"
#include "BLEScan.h"
#include "BLERemoteService.h"
#include <cstring>

// Nordic UART Service UUIDs
static const BLEUUID kNUS_ServiceUUID("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static const BLEUUID kNUS_RxCharUUID("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static const BLEUUID kNUS_TxCharUUID("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

BLEUUID BLEStream::nusServiceUUID() { return kNUS_ServiceUUID; }
BLEUUID BLEStream::nusRxCharUUID() { return kNUS_RxCharUUID; }
BLEUUID BLEStream::nusTxCharUUID() { return kNUS_TxCharUUID; }

// --------------------------------------------------------------------------
// Ring buffer
// --------------------------------------------------------------------------

struct RingBuffer {
  uint8_t *buf;
  size_t capacity;
  volatile size_t head = 0;
  volatile size_t tail = 0;

  explicit RingBuffer(size_t cap) : capacity(cap) {
    buf = new uint8_t[cap];
  }
  ~RingBuffer() { delete[] buf; }

  size_t available() const {
    size_t h = head, t = tail;
    return (h >= t) ? (h - t) : (capacity - t + h);
  }

  bool push(uint8_t c) {
    size_t next = (head + 1) % capacity;
    if (next == tail) return false; // full
    buf[head] = c;
    head = next;
    return true;
  }

  size_t push(const uint8_t *data, size_t len) {
    size_t written = 0;
    for (size_t i = 0; i < len; i++) {
      if (!push(data[i])) break;
      written++;
    }
    return written;
  }

  int peek() const {
    if (head == tail) return -1;
    return buf[tail];
  }

  int pop() {
    if (head == tail) return -1;
    uint8_t c = buf[tail];
    tail = (tail + 1) % capacity;
    return c;
  }
};

// --------------------------------------------------------------------------
// Impl
// --------------------------------------------------------------------------

struct BLEStream::Impl {
  enum class Mode { None, Server, Client } mode = Mode::None;

  // Server mode
  BLEServer server;
  BLECharacteristic txChr;  // Server TX (Notify) -> client receives
  BLECharacteristic rxChr;  // Server RX (Write)  -> client sends

  // Client mode
  BLEClient client;
  BLERemoteCharacteristic remoteTx;  // Remote TX (subscribe for notifications)
  BLERemoteCharacteristic remoteRx;  // Remote RX (write to send data)

  RingBuffer rxBuf;
  volatile bool isConnected = false;

  ConnectHandler onConnectCb = nullptr;
  DisconnectHandler onDisconnectCb = nullptr;

  explicit Impl(size_t rxBufSize) : rxBuf(rxBufSize) {}
};

// --------------------------------------------------------------------------
// BLEStream lifecycle
// --------------------------------------------------------------------------

BLEStream::BLEStream(size_t rxBufferSize) : _impl(new Impl(rxBufferSize)) {}

BLEStream::~BLEStream() {
  end();
  delete _impl;
}

BTStatus BLEStream::begin(const String &deviceName) {
  if (!_impl || _impl->mode != Impl::Mode::None) return BTStatus::InvalidState;

  // Initialize BLE if not already
  if (!BLE.isInitialized()) {
    BTStatus s = BLE.begin(deviceName);
    if (!s) return s;
  }

  _impl->server = BLE.createServer();
  if (!_impl->server) return BTStatus::Fail;

  _impl->server.onConnect([this](BLEServer, const BLEConnInfo &) {
    _impl->isConnected = true;
    if (_impl->onConnectCb) _impl->onConnectCb();
  });

  _impl->server.onDisconnect([this](BLEServer, const BLEConnInfo &, uint8_t) {
    _impl->isConnected = false;
    if (_impl->onDisconnectCb) _impl->onDisconnectCb();
  });

  _impl->server.advertiseOnDisconnect(true);

  BLEService svc = _impl->server.createService(kNUS_ServiceUUID);

  // TX characteristic: server notifies, client reads
  _impl->txChr = svc.createCharacteristic(kNUS_TxCharUUID, BLEProperty::Notify);

  // RX characteristic: client writes, server receives
  _impl->rxChr = svc.createCharacteristic(kNUS_RxCharUUID, BLEProperty::Write | BLEProperty::WriteNR);
  _impl->rxChr.onWrite([this](BLECharacteristic chr, const BLEConnInfo &) {
    size_t len = 0;
    const uint8_t *data = chr.getValue(&len);
    if (data && len > 0) {
      _impl->rxBuf.push(data, len);
    }
  });

  svc.start();

  // Stop advertising before (re-)starting the server so the GATT table is
  // mutable (NimBLE requires no active GAP procedures during registration).
  BLEAdvertising adv = BLE.getAdvertising();
  adv.stop();

  BTStatus srvStatus = _impl->server.start();
  if (!srvStatus) return srvStatus;

  // Reconfigure advertising to include the NUS service UUID.
  adv.reset();
  adv.setName(deviceName);
  adv.addServiceUUID(kNUS_ServiceUUID);
  BTStatus advStatus = adv.start();
  if (!advStatus) {
    return advStatus;
  }

  _impl->mode = Impl::Mode::Server;
  return BTStatus::OK;
}

BTStatus BLEStream::beginClient(const BTAddress &address, uint32_t timeoutMs) {
  if (!_impl || _impl->mode != Impl::Mode::None) return BTStatus::InvalidState;

  // Initialize BLE if not already
  if (!BLE.isInitialized()) {
    BTStatus s = BLE.begin("");
    if (!s) return s;
  }

  _impl->client = BLE.createClient();
  if (!_impl->client) return BTStatus::Fail;

  _impl->client.onConnect([this](BLEClient, const BLEConnInfo &) {
    _impl->isConnected = true;
    if (_impl->onConnectCb) _impl->onConnectCb();
  });

  _impl->client.onDisconnect([this](BLEClient, const BLEConnInfo &, uint8_t) {
    _impl->isConnected = false;
    if (_impl->onDisconnectCb) _impl->onDisconnectCb();
  });

  BTStatus s = _impl->client.connect(address, timeoutMs);
  if (!s) return s;

  // Discover NUS service
  BLERemoteService svc = _impl->client.getService(kNUS_ServiceUUID);
  if (!svc) {
    _impl->client.disconnect();
    return BTStatus::NotFound;
  }

  _impl->remoteTx = svc.getCharacteristic(kNUS_TxCharUUID);
  _impl->remoteRx = svc.getCharacteristic(kNUS_RxCharUUID);

  if (!_impl->remoteTx || !_impl->remoteRx) {
    _impl->client.disconnect();
    return BTStatus::NotFound;
  }

  // Subscribe to TX notifications (data from server -> us)
  _impl->remoteTx.subscribe(true, [this](BLERemoteCharacteristic, const uint8_t *data, size_t len, bool) {
    if (data && len > 0) {
      _impl->rxBuf.push(data, len);
    }
  });

  _impl->mode = Impl::Mode::Client;
  return BTStatus::OK;
}

void BLEStream::end() {
  if (!_impl) return;
  if (_impl->mode == Impl::Mode::Client && _impl->client) {
    _impl->client.disconnect();
  }
  _impl->isConnected = false;
  _impl->mode = Impl::Mode::None;
}

bool BLEStream::connected() const {
  return _impl && _impl->isConnected;
}

// --------------------------------------------------------------------------
// Arduino Stream interface
// --------------------------------------------------------------------------

int BLEStream::available() {
  return _impl ? static_cast<int>(_impl->rxBuf.available()) : 0;
}

int BLEStream::read() {
  return _impl ? _impl->rxBuf.pop() : -1;
}

int BLEStream::peek() {
  return _impl ? _impl->rxBuf.peek() : -1;
}

size_t BLEStream::write(uint8_t c) {
  return write(&c, 1);
}

size_t BLEStream::write(const uint8_t *buffer, size_t size) {
  if (!_impl || !_impl->isConnected || size == 0) return 0;

  if (_impl->mode == Impl::Mode::Server) {
    // Server sends via TX characteristic notifications
    // Chunk at MTU - 3 (ATT header overhead)
    uint16_t mtu = BLE.getMTU();
    uint16_t chunkSize = (mtu > 3) ? (mtu - 3) : 20;
    size_t sent = 0;

    while (sent < size) {
      size_t len = size - sent;
      if (len > chunkSize) len = chunkSize;

      _impl->txChr.setValue(buffer + sent, len);
      BTStatus s = _impl->txChr.notify();
      if (!s) break;
      sent += len;
    }
    return sent;
  } else if (_impl->mode == Impl::Mode::Client) {
    // Client sends via RX characteristic writes
    uint16_t mtu = _impl->client.getMTU();
    uint16_t chunkSize = (mtu > 3) ? (mtu - 3) : 20;
    size_t sent = 0;

    while (sent < size) {
      size_t len = size - sent;
      if (len > chunkSize) len = chunkSize;

      BTStatus s = _impl->remoteRx.writeValue(buffer + sent, len, false);
      if (!s) break;
      sent += len;
    }
    return sent;
  }

  return 0;
}

void BLEStream::flush() {
  // BLE notifications are sent immediately; no buffering to flush
}

// --------------------------------------------------------------------------
// Callbacks
// --------------------------------------------------------------------------

void BLEStream::onConnect(ConnectHandler handler) {
  if (_impl) _impl->onConnectCb = handler;
}

void BLEStream::onDisconnect(DisconnectHandler handler) {
  if (_impl) _impl->onDisconnectCb = handler;
}

#endif /* BLE_ENABLED */
