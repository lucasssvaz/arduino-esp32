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
 * @brief Internal helpers shared between BLEAudioStream and the role handles.
 *
 * Role handles (unicast server/client, broadcast) create directional streams
 * and register them in a small per-direction dispatch registry so the single
 * set of global BAP vendor callbacks (`bleBapVendorSetStreamCbs`) can route
 * started/stopped/recv/sent events onto the right `BLEAudioStream::Impl`. Not
 * part of the public API.
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include <functional>
#include "audio/BLEAudioStream.h"

namespace BLEAudioStreamInternal {

/** Data-flow direction index matching BLE_BAP_VENDOR_DIR_*. */
static constexpr uint8_t DIR_SINK = 0;
static constexpr uint8_t DIR_SOURCE = 1;

/** SDU transmit function a source stream routes `write()` through. */
using SendFn = std::function<int(const uint8_t *sdu, uint16_t len, uint16_t seqNum)>;

/** @brief Allocate a fresh stream Impl bound to a direction. */
std::shared_ptr<BLEAudioStream::Impl> makeStream(uint8_t dir);

/** @brief Register a stream Impl as the dispatch target for its direction. */
void registerForDispatch(const std::shared_ptr<BLEAudioStream::Impl> &impl);

/** @brief Clear both direction slots (call on role teardown). */
void clearDispatch();

/**
 * @brief Override the SDU sender a source stream uses for `write()`.
 *
 * Unicast source streams keep the default (unicast BAP vendor send); the
 * broadcast source role points its stream at the broadcast vendor send.
 */
void setSender(const std::shared_ptr<BLEAudioStream::Impl> &impl, SendFn sender);

/** @brief Install the unicast BAP vendor stream callbacks routing into the registry. */
void installVendorStreamCbs();

/** @brief Install the broadcast BAP vendor stream callbacks routing into the registry. */
void installBroadcastVendorStreamCbs();

}  // namespace BLEAudioStreamInternal

#endif /* BLE_AUDIO_SUPPORTED */
