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
 * @brief Top-level LE Audio profile identity handles: TMAP and GMAP.
 *
 * These profiles sit at the very top of the Generic Audio Framework. They do
 * not move audio themselves -- CAP + BAP + the control profiles do that. What
 * they add is a small "identity" service (TMAS / GMAS) advertising which
 * top-level roles a device implements, plus a client-side discovery to read a
 * peer's roles. A spec-compliant telephony/media or gaming device therefore
 * registers the matching role(s) here in addition to bringing up its CAP
 * acceptor/initiator + data-plane + control roles.
 *
 * Both handles are shared-handle value types minted by the `BLEAudio`
 * controller (like the other role handles), and follow the same lifecycle:
 * configure roles before `audio.start()`; discover after an ACL connection.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include "BTStatus.h"

class BLEAudio;

/**
 * @brief TMAP (Telephony and Media Audio Profile) roles, as a bitmask.
 *
 * Bit positions match the spec/engine exactly. Combine with `operator|`.
 */
enum class BLEAudioTmapRole : uint8_t {
  None = 0x00,
  CallGateway = 0x01,             //!< CG: places/controls calls on a Call Terminal
  CallTerminal = 0x02,            //!< CT: the phone/headset end of a call
  UnicastMediaSender = 0x04,      //!< UMS: streams media to a receiver
  UnicastMediaReceiver = 0x08,    //!< UMR: receives streamed media
  BroadcastMediaSender = 0x10,    //!< BMS: Auracast media transmitter
  BroadcastMediaReceiver = 0x20,  //!< BMR: Auracast media receiver
};

inline BLEAudioTmapRole operator|(BLEAudioTmapRole a, BLEAudioTmapRole b) {
  return static_cast<BLEAudioTmapRole>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline BLEAudioTmapRole &operator|=(BLEAudioTmapRole &a, BLEAudioTmapRole b) {
  a = a | b;
  return a;
}
inline bool operator&(BLEAudioTmapRole a, BLEAudioTmapRole b) {
  return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

/**
 * @brief GMAP (Gaming Audio Profile) roles, as a bitmask.
 *
 * Bit positions match the spec/engine exactly. Combine with `operator|`.
 */
enum class BLEAudioGmapRole : uint8_t {
  None = 0x00,
  UnicastGameGateway = 0x01,     //!< UGG: the console/PC end of a game link
  UnicastGameTerminal = 0x02,    //!< UGT: the headset end of a game link
  BroadcastGameSender = 0x04,    //!< BGS
  BroadcastGameReceiver = 0x08,  //!< BGR
};

inline BLEAudioGmapRole operator|(BLEAudioGmapRole a, BLEAudioGmapRole b) {
  return static_cast<BLEAudioGmapRole>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline BLEAudioGmapRole &operator|=(BLEAudioGmapRole &a, BLEAudioGmapRole b) {
  a = a | b;
  return a;
}
inline bool operator&(BLEAudioGmapRole a, BLEAudioGmapRole b) {
  return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

/**
 * @brief TMAP identity handle (both server and client sides).
 */
class BLEAudioTmap {
public:
  /** Called when discovery of a peer's TMAS completes. */
  using DiscoverCallback = std::function<void(BTStatus status, BLEAudioTmapRole peerRoles)>;

  BLEAudioTmap();
  ~BLEAudioTmap() = default;
  BLEAudioTmap(const BLEAudioTmap &) = default;
  BLEAudioTmap &operator=(const BLEAudioTmap &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /**
   * @brief Set the local TMAP role(s) advertised by the TMAS instance.
   *        Must be called before `audio.start()`.
   */
  BLEAudioTmap &setRoles(BLEAudioTmapRole roles);

  /**
   * @brief Discover the peer's TMAS on an established ACL connection.
   * @param connHandle ACL connection handle (e.g. `BLEClient::getHandle()`).
   * @return BTStatus::OK if discovery started, or an error code.
   */
  BTStatus discover(uint16_t connHandle);

  /** @brief Set the discovery-complete callback. */
  BLEAudioTmap &onDiscovered(DiscoverCallback cb);

  struct Impl;

private:
  explicit BLEAudioTmap(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

/**
 * @brief GMAP identity handle (client discover + stubbed server publish).
 *
 * @warning Server-side GMAS is **stubbed** on packaged ESP-IDF `release/v6.1`
 *          libs (no host-adapter `gmas.c`). `setRoles()`/`setFeatures()` do not
 *          publish `0x1858`; register returns `-ENOTSUP` and `audio.start()`
 *          continues. Client `discover()` against a peer that exposes GMAS is
 *          unaffected when `CONFIG_BT_GMAP` is enabled. See `AUDIO.md`
 *          "Engine gaps".
 */
class BLEAudioGmap {
public:
  /** Called when discovery of a peer's GMAS completes. */
  using DiscoverCallback = std::function<void(BTStatus status, BLEAudioGmapRole peerRoles)>;

  BLEAudioGmap();
  ~BLEAudioGmap() = default;
  BLEAudioGmap(const BLEAudioGmap &) = default;
  BLEAudioGmap &operator=(const BLEAudioGmap &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /**
   * @brief Request local GMAP roles (stubbed on packaged libs — no GMAS).
   *
   * Must be called before `audio.start()` if used. On current libs this is a
   * no-op stub: no GMAS service is committed.
   */
  BLEAudioGmap &setRoles(BLEAudioGmapRole roles);

  /**
   * @brief Set per-role feature bitmasks (optional; ignored while server publish is stubbed).
   */
  BLEAudioGmap &setFeatures(uint8_t unicastGatewayFeat, uint8_t unicastTerminalFeat, uint8_t broadcastSenderFeat, uint8_t broadcastReceiverFeat);

  /**
   * @brief Discover the peer's GMAS on an established ACL connection.
   * @param connHandle ACL connection handle (e.g. `BLEClient::getHandle()`).
   * @return BTStatus::OK if discovery started, or an error code.
   */
  BTStatus discover(uint16_t connHandle);

  /** @brief Set the discovery-complete callback. */
  BLEAudioGmap &onDiscovered(DiscoverCallback cb);

  struct Impl;

private:
  explicit BLEAudioGmap(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

/**
 * @brief PBP (Public Broadcast Profile) announcement helper.
 *
 * Stateless build/parse of the Public Broadcast Announcement (PBA) service-data
 * structure. The turnkey `BLEAudioBroadcastSource` also injects a PBA (`0x1856`)
 * into the extended advertising set it owns (phones' Auracast UIs require it).
 */
class BLEAudioPublicBroadcast {
public:
  /** PBP announcement feature bits (combine with `|`). */
  enum Feature : uint8_t {
    Encryption = 0x01,
    StandardQuality = 0x02,
    HighQuality = 0x04,
  };

  /**
   * @brief Build a PBA service-data (0x16) AD value.
   * @param features Feature bits (see Feature).
   * @param metadata Optional LTV metadata to embed (may be null).
   * @param metadataLen Length of @p metadata.
   * @param out Output buffer for the AD value.
   * @param outCap Capacity of @p out.
   * @return Number of bytes written, or 0 on error.
   */
  static size_t buildAnnouncement(uint8_t features, const uint8_t *metadata, size_t metadataLen, uint8_t *out, size_t outCap);

  /**
   * @brief Parse a received PBA service-data (0x16) AD value.
   * @param data The AD value bytes.
   * @param dataLen Length of @p data.
   * @param featuresOut Receives the parsed feature bits.
   * @param metadataOut Receives a pointer into @p data to the metadata (or null).
   * @param metadataLenOut Receives the metadata length.
   * @return true on success.
   */
  static bool parseAnnouncement(const uint8_t *data, size_t dataLen, uint8_t &featuresOut, const uint8_t *&metadataOut, size_t &metadataLenOut);
};

#endif /* BLE_AUDIO_SUPPORTED */
