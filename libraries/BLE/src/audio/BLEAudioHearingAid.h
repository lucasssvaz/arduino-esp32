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
 * @brief Hearing Access Service (HAS) role handles: hearing-aid device (server)
 *        and controller (client).
 *
 * A hearing-aid device publishes a set of named "presets" (listening programs)
 * and lets a controller switch the active one. Minted by the `BLEAudio`
 * controller like the other role handles: configure presets before
 * `audio.start()`; drive the peer after an ACL connection.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "BTStatus.h"

class BLEAudio;

/** Hearing-aid device type (spec "Hearing Aid Type"). */
enum class BLEHearingAidType : uint8_t {
  Binaural = 0x00,  //!< A left/right pair forming a coordinated set
  Monaural = 0x01,  //!< A single hearing aid
  Banded = 0x02,    //!< Two aids sharing one radio (single interface)
};

/**
 * @brief HAS hearing-aid device (server) role handle.
 */
class BLEAudioHearingAidDevice {
public:
  /** Called when a controller requests activation of preset @p index. */
  using SelectCallback = std::function<void(uint8_t index, bool sync)>;

  BLEAudioHearingAidDevice();
  ~BLEAudioHearingAidDevice() = default;
  BLEAudioHearingAidDevice(const BLEAudioHearingAidDevice &) = default;
  BLEAudioHearingAidDevice &operator=(const BLEAudioHearingAidDevice &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  // --- Configuration (before audio.start()) ---

  /** @brief Set the hearing-aid type (default Monaural). */
  BLEAudioHearingAidDevice &setType(BLEHearingAidType type);
  /** @brief Enable preset synchronization across a binaural set (default off). */
  BLEAudioHearingAidDevice &setPresetSync(bool enabled);
  /**
   * @brief Add a preset record exposed by the service.
   * @param index Unique preset index (>= 1).
   * @param name Preset name (spec length limits apply).
   * @param available Whether the preset is selectable (default true).
   * @param writable Whether the client may rename it (default false).
   */
  BLEAudioHearingAidDevice &addPreset(uint8_t index, const std::string &name, bool available = true, bool writable = false);

  // --- Runtime control (after audio.start()) ---

  /** @brief Set the active preset by index. */
  BTStatus setActivePreset(uint8_t index);
  /** @brief Get the active preset index (0 = none). */
  uint8_t getActivePreset() const;

  /** @brief Set the callback invoked when a controller selects a preset. */
  BLEAudioHearingAidDevice &onPresetSelected(SelectCallback cb);

  struct Impl;

private:
  explicit BLEAudioHearingAidDevice(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

/**
 * @brief HAS hearing-aid controller (client) role handle.
 */
class BLEAudioHearingAidController {
public:
  /** Called when discovery completes (type/caps from the peer). */
  using DiscoverCallback = std::function<void(BTStatus status, BLEHearingAidType type)>;
  /** Called for each preset record read/notified (name is transient). */
  using PresetCallback = std::function<void(uint8_t index, bool available, const std::string &name, bool isLast)>;
  /** Called when the peer's active preset changes. */
  using SwitchCallback = std::function<void(BTStatus status, uint8_t index)>;

  BLEAudioHearingAidController();
  ~BLEAudioHearingAidController() = default;
  BLEAudioHearingAidController(const BLEAudioHearingAidController &) = default;
  BLEAudioHearingAidController &operator=(const BLEAudioHearingAidController &) = default;

  /** @brief Whether this handle references a live role. */
  explicit operator bool() const;

  /**
   * @brief Discover a peer's HAS on an established ACL connection.
   * @param connHandle ACL connection handle (e.g. `BLEClient::getHandle()`).
   */
  BTStatus discover(uint16_t connHandle);

  /** @brief Read up to @p maxCount presets starting at @p startIndex. */
  BTStatus readPresets(uint8_t startIndex = 1, uint8_t maxCount = 255);
  /** @brief Set the active preset by index on the peer. */
  BTStatus setActivePreset(uint8_t index, bool sync = false);
  /** @brief Activate the next available preset on the peer. */
  BTStatus nextPreset(bool sync = false);
  /** @brief Activate the previous available preset on the peer. */
  BTStatus previousPreset(bool sync = false);

  /** @brief Set the discovery-complete callback. */
  BLEAudioHearingAidController &onDiscovered(DiscoverCallback cb);
  /** @brief Set the preset-record callback (fires per record). */
  BLEAudioHearingAidController &onPreset(PresetCallback cb);
  /** @brief Set the active-preset-changed callback. */
  BLEAudioHearingAidController &onPresetSwitch(SwitchCallback cb);

  struct Impl;

private:
  explicit BLEAudioHearingAidController(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEAudio;
};

#endif /* BLE_AUDIO_SUPPORTED */
