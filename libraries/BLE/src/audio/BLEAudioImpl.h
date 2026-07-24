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
 * @brief Shared implementation state for the LE Audio controller handle.
 *
 * The audio component is "fully shared" (like BLEAdvertisedDevice): the engine
 * API is host-agnostic, so the concrete `BLEAudio::Impl` is defined ONCE here
 * and compiled into every build, rather than once per backend. It follows the
 * two-layer PIMPL contract (`FooImplCommon` base + `Foo::Impl`) but carries no
 * backend/vendor types -- all engine calls live in BLEAudioEngine.
 */

#include "audio/BLEAudio.h"
#if BLE_AUDIO_SUPPORTED

#include <cstdint>
#include <functional>
#include <vector>
#include "BTStatus.h"

struct BLEAudioImplCommon {
  uint32_t presentationDelayUs = 40000;  ///< Global default presentation delay.
  bool active = false;                   ///< Whether begin() has completed.

  // Role "apply" hooks staged by role handles between begin() and start().
  // BLEAudio::start() runs each (registering PACS/ASCS/client with the engine)
  // just before the single coordinated commit, matching "accumulate then
  // commit". Cleared after they run so a later start() does not re-apply.
  std::vector<std::function<BTStatus()>> roleApplies;
};

// Fully-shared: the neutral combiner just adopts the shared base so all state
// is disclosed as common by its type (no per-backend Impl exists).
struct BLEAudio::Impl : BLEAudioImplCommon {};

#endif /* BLE_AUDIO_SUPPORTED */
