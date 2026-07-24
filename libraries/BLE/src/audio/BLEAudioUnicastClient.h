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
 * @brief BAP Unicast Client (initiator / central) role handle.
 *
 * The Unicast Client discovers a peer's PACS/ASCS, configures a codec + QoS,
 * enables the streams, opens the CIS, and starts streaming. Minted by
 * `BLEAudio::createUnicastClient()`. After the controller `start()` and an ACL
 * connection to a unicast server (via `BLEClient`), call `connect(handle)` to
 * run the whole setup sequence; observe/drive the streams via the handles.
 *
 * Backend-agnostic: no `esp_ble_audio_*` type appears here; the intricate
 * discover->config->qos->enable->connect->start orchestration lives behind the
 * C-safe `BLEAudioBapVendor` boundary.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include <cstdint>
#include "BTStatus.h"
#include "audio/BLEAudioTypes.h"
#include "audio/BLEAudioStream.h"

class BLEAudio;

class BLEAudioUnicastClient {
public:
  BLEAudioUnicastClient();
  ~BLEAudioUnicastClient() = default;
  BLEAudioUnicastClient(const BLEAudioUnicastClient &) = default;
  BLEAudioUnicastClient &operator=(const BLEAudioUnicastClient &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /** @brief Choose the LC3 preset to request (default LC3_16_2_1). */
  BLEAudioUnicastClient &setPreset(BLEAudioCodecPreset preset);

  /**
   * @brief Run the unicast stream setup on an established ACL connection.
   *
   * Kicks discovery of the peer's ASEs; the engine then chains config -> QoS ->
   * enable -> CIS connect -> start. Progress surfaces through the stream
   * callbacks (`txStream()` becomes streaming once the CIS is up).
   *
   * @param connHandle ACL connection handle (e.g. `BLEClient::getHandle()`).
   * @return BTStatus::OK if setup started, or an error code.
   */
  BTStatus connect(uint16_t connHandle);

  /** @brief Tear down the client's group/stream state (call on disconnect). */
  void reset();

  /** @brief The transmit stream (SDUs sent to the peer's sink ASE). */
  BLEAudioStream txStream() const;
  /** @brief The receive stream (SDUs from the peer's source ASE). */
  BLEAudioStream rxStream() const;

  struct Impl;

private:
  explicit BLEAudioUnicastClient(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

#endif /* BLE_AUDIO_SUPPORTED */
