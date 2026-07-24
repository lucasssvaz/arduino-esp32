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
 * @brief Call Control Profile (CCP) role handles — **GTBS only**.
 *
 * Two shared-handle roles, both minted by the `BLEAudio` controller:
 *  - `BLEAudioCallServer` (CCP server / GTBS): publishes a Generic Telephone
 *    Bearer, announces incoming calls, and answers client call-control writes.
 *  - `BLEAudioCallController` (CCP client): discovers a peer's GTBS and drives
 *    calls (originate / accept / terminate).
 *
 * Discrete (non-generic) TBS is not exposed: packaged IDF `release/v6.1` has no
 * discrete TBS host adapter (added on upstream `master`). See `AUDIO.md`.
 *
 * Backend-agnostic: no `esp_ble_audio_*` type appears here; the engine work
 * happens through the C-safe `BLEAudioCallVendor` boundary.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include <cstdint>
#include <functional>
#include <string>
#include "BTStatus.h"

class BLEAudio;

/**
 * @brief CCP Server (GTBS) role handle.
 */
class BLEAudioCallServer {
public:
  /**
   * @brief Called when a client requests to originate a call. Return true to accept.
   *
   * @warning Never invoked in this build. The engine instantiates only the
   *          Generic Telephone Bearer (CONFIG_BT_TBS_BEARER_COUNT is pinned to
   *          0), and GTBS does not place calls itself -- it routes an originate
   *          to whichever telephone bearer supports the requested URI scheme.
   *          The scheme list is a per-bearer property, so with no bearers no
   *          scheme can ever match and the server answers every Originate with
   *          Invalid Outgoing URI (0x06). Calls the device itself raises with
   *          incomingCall(), and the peer accepting or terminating them, work
   *          normally.
   */
  using OriginateCallback = std::function<bool(uint8_t callIndex, const std::string &uri)>;
  /** Called when a call is terminated. */
  using TerminateCallback = std::function<void(uint8_t callIndex, uint8_t reason)>;

  BLEAudioCallServer();
  ~BLEAudioCallServer() = default;
  BLEAudioCallServer(const BLEAudioCallServer &) = default;
  BLEAudioCallServer &operator=(const BLEAudioCallServer &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  // --- Configuration (before audio.start()) ---

  /** @brief Set the bearer provider name. Default "ESP Phone". */
  BLEAudioCallServer &setProviderName(const std::string &name);
  /** @brief Set the Uniform Caller Identifier. Default "un000". */
  BLEAudioCallServer &setUci(const std::string &uci);

  // --- Runtime (after audio.start()) ---

  /** @brief Announce an incoming call; returns the assigned call index. */
  BTStatus incomingCall(const std::string &from, uint8_t &callIndex);
  /** @brief Terminate a call by index. */
  BTStatus terminate(uint8_t callIndex);
  /** @brief Update the bearer provider name at runtime. */
  BTStatus updateProviderName(const std::string &name);

  /** @brief Set the originate-request callback. */
  BLEAudioCallServer &onOriginate(OriginateCallback cb);
  /** @brief Set the terminate callback. */
  BLEAudioCallServer &onTerminated(TerminateCallback cb);

  struct Impl;

private:
  explicit BLEAudioCallServer(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

/**
 * @brief CCP Client (TBS client) role handle.
 */
class BLEAudioCallController {
public:
  /** Call-control operation kinds reported to the result callback. */
  enum class Operation : uint8_t {
    Originate = 0,
    Accept,
    Terminate,
    Hold,
    Retrieve,
  };

  /** Called when GTBS/TBS discovery completes. */
  using DiscoverCallback = std::function<void(BTStatus status, bool gtbsFound)>;
  /** Called when a call-control operation completes. */
  using ResultCallback = std::function<void(Operation op, BTStatus status, uint8_t callIndex)>;

  BLEAudioCallController();
  ~BLEAudioCallController() = default;
  BLEAudioCallController(const BLEAudioCallController &) = default;
  BLEAudioCallController &operator=(const BLEAudioCallController &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /** @brief Discover GTBS/TBS on an established ACL link. */
  BTStatus discover(uint16_t connHandle);
  /** @brief Originate a call to @p uri on the peer. */
  BTStatus originate(uint16_t connHandle, const std::string &uri);
  /** @brief Accept an incoming call by index on the peer. */
  BTStatus accept(uint16_t connHandle, uint8_t callIndex);
  /** @brief Terminate a call by index on the peer. */
  BTStatus terminate(uint16_t connHandle, uint8_t callIndex);

  /** @brief Set the discovery-complete callback. */
  BLEAudioCallController &onDiscovered(DiscoverCallback cb);
  /** @brief Set the operation-result callback. */
  BLEAudioCallController &onResult(ResultCallback cb);

  struct Impl;

private:
  explicit BLEAudioCallController(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

#endif /* BLE_AUDIO_SUPPORTED */
