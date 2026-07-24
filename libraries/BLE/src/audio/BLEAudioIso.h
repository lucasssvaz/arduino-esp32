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

#pragma once

/**
 * @file
 * @brief Internal, audio-agnostic Isochronous (ISO) transport layer.
 *
 * A thin, host-agnostic C++ wrapper over the ESP-BLE-ISO engine that speaks in
 * ISO primitives only -- channels, connected isochronous streams (CIS),
 * broadcast isochronous streams (BIS), and transparent SDUs. It carries no
 * LC3/BAP/audio concept, so the same code can later be promoted to a public
 * `BLEIso*` feature (its own headers, guards, examples) without rework; for now
 * it lives under `audio/` and is consumed only beneath the (future) BAP /
 * broadcast profile layer.
 *
 * Fully shared across NimBLE and Bluedroid: the ESP-BLE-ISO API is
 * host-agnostic. The vendor Zephyr headers do not parse as C++, so the actual
 * `esp_ble_iso_*` calls live in the C translation unit `BLEAudioIsoVendor.c`;
 * this layer reaches them through the C-safe `BLEAudioIsoVendor.h` surface and
 * maps results into `BTStatus`.
 *
 * Roles supported:
 *  - CIS Central    (`connectCis`)  -- creates the CIG and connects a CIS on an
 *                                      existing ACL link (from `BLEClient`).
 *  - CIS Peripheral (`listenCis`)   -- registers the ISO server; the CIS is
 *                                      accepted when the Central establishes it.
 *  - BIS Broadcaster(`createBig`)   -- creates a BIG on a running ext/periodic
 *                                      advertising instance (from `BLEAdvertising`).
 *  - BIS Receiver   (`syncBig`)     -- arms a BIG sync; the engine issues it when
 *                                      the BIGInfo report arrives on the periodic
 *                                      train the host is synced to (`BLEScan`).
 *
 * The one genuinely backend-specific behavior -- forwarding raw NimBLE GAP
 * events into the engine so the BIS receiver observes the BIGInfo report -- is
 * kept out of this shared header: it lives in the NimBLE-only glue
 * `BLEAudioIso.nimble.{h,cpp}` (mirroring `BLEAudioEngine.nimble.*`), so this
 * header never drags a stack type in.
 */

#include "core/BLEGuards.h"
#if BLE_ISO_SUPPORTED

#include <cstdint>
#include <cstddef>
#include <functional>
#include "BTStatus.h"

namespace BLEAudioIso {

/** Maximum concurrent ISO channels (mirrors the C pool). */
static constexpr uint8_t MAX_CHANNELS = 4;

/** PHY selector for ISO QoS. */
enum class Phy : uint8_t {
  Phy1M = 1,
  Phy2M = 2,
  PhyCoded = 3,
};

/** Receive metadata for one transparent SDU. */
struct SduInfo {
  uint32_t timestampUs = 0;   /*!< Controller timestamp (only if timestampValid). */
  uint16_t seqNum = 0;        /*!< SDU sequence number of the first fragment. */
  bool valid = false;         /*!< Payload is complete and error-free. */
  bool timestampValid = false;/*!< The timestamp field is meaningful. */
};

/**
 * @brief One ISO channel (a CIS or a single BIS).
 *
 * Not directly constructed: obtained from the transport factories, which return
 * a pointer to a pool-owned instance whose lifetime is the transport session
 * (until `end()`). Callbacks fire from the ISO host task.
 */
class Channel {
 public:
  using ConnectedCb = std::function<void(Channel &)>;
  using DisconnectedCb = std::function<void(Channel &, uint8_t reason)>;
  using ReceiveCb = std::function<void(Channel &, const SduInfo &, const uint8_t *sdu, uint16_t len)>;
  using SentCb = std::function<void(Channel &)>;

  /** @brief Pool slot index (stable for the channel's lifetime), or -1. */
  int slot() const {
    return _slot;
  }

  /** @brief Whether the underlying CIS/BIS is currently established. */
  bool isConnected() const;

  /**
   * @brief Send one transparent SDU. Valid once the channel is connected.
   * @param seqNum Monotonically increasing per-channel SDU sequence number.
   */
  BTStatus send(const uint8_t *sdu, uint16_t len, uint16_t seqNum);

  void onConnected(ConnectedCb cb) {
    _onConnected = std::move(cb);
  }
  void onDisconnected(DisconnectedCb cb) {
    _onDisconnected = std::move(cb);
  }
  void onReceive(ReceiveCb cb) {
    _onReceive = std::move(cb);
  }
  void onSent(SentCb cb) {
    _onSent = std::move(cb);
  }

  // Internal: invoked by the vendor dispatch. Not for application use.
  void _dispatchConnected();
  void _dispatchDisconnected(uint8_t reason);
  void _dispatchReceive(const SduInfo &info, const uint8_t *sdu, uint16_t len);
  void _dispatchSent();
  void _bind(int slot) {
    _slot = slot;
  }
  void _reset();

 private:
  int _slot = -1;
  ConnectedCb _onConnected;
  DisconnectedCb _onDisconnected;
  ReceiveCb _onReceive;
  SentCb _onSent;
};

/** Connected Isochronous Stream (CIS) QoS. */
struct CisParams {
  uint32_t sduIntervalUs = 10000;  /*!< SDU interval, microseconds. */
  uint16_t latencyMs = 10;         /*!< Transport latency, milliseconds. */
  uint16_t sduSize = 120;          /*!< Max SDU size, octets. */
  Phy phy = Phy::Phy2M;
  uint8_t rtn = 2;                 /*!< Retransmission number. */
  uint8_t packing = 0;             /*!< 0 sequential, 1 interleaved. */
  uint8_t framing = 0;             /*!< 0 unframed, 1 framed. */
};

/** Broadcast Isochronous Stream (BIS) QoS. */
struct BigParams {
  uint32_t sduIntervalUs = 10000;
  uint16_t latencyMs = 10;
  uint16_t sduSize = 120;
  Phy phy = Phy::Phy2M;
  uint8_t rtn = 2;
  uint8_t packing = 0;
  uint8_t framing = 0;
  const uint8_t *broadcastCode = nullptr;  /*!< Optional (encryption); NULL disables it. */
  uint8_t broadcastCodeLen = 0;
};

/** @brief Initialize the ISO transport engine (idempotent). */
BTStatus begin();

/** @brief Tear the transport down and release every channel. */
void end();

/** @brief Whether the transport is initialized. */
bool isActive();

/**
 * @brief Create the CIG and connect a CIS as Central on an existing ACL link.
 * @param connHandle ACL connection handle (e.g. `BLEClient::getHandle()`).
 * @return The channel, or nullptr on error. `onConnected` fires when the CIS is up.
 */
Channel *connectCis(uint16_t connHandle, const CisParams &params);

/**
 * @brief Register the ISO server and accept an inbound CIS as Peripheral.
 * @return The channel, or nullptr on error. `onConnected` fires on acceptance.
 */
Channel *listenCis(const CisParams &params);

/**
 * @brief Create a BIG (Broadcaster) on a running ext/periodic advertising set.
 * @param advHandle Advertising instance carrying the periodic train.
 * @return The channel, or nullptr on error. `onConnected` fires when the BIS is up.
 */
Channel *createBig(uint8_t advHandle, const BigParams &params);

/**
 * @brief Arm a BIG sync (Receiver) for one BIS index on the synced periodic train.
 * @param bisIndex 1-based BIS index to receive.
 * @return The channel, or nullptr on error. `onConnected` fires when synced.
 */
Channel *syncBig(uint8_t bisIndex, const BigParams &params);

}  // namespace BLEAudioIso

#endif /* BLE_ISO_SUPPORTED */
