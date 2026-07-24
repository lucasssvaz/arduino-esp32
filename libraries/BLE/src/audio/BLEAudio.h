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
 * @brief LE Audio controller handle -- the entry point for the audio subsystem.
 *
 * `BLEAudio` is NOT a second global singleton. It is a shared handle minted
 * from the `BLE` singleton via `BLE.getAudioController()` (the same
 * singleton-getter model as `BLE.getScan()` / `BLE.getSecurity()`): the first
 * call allocates the single engine-backed implementation, later calls return a
 * handle to the same instance. It is only valid after `BLE.begin()`.
 *
 * This header is also the audio component's aggregator header: `src/BLE.h`
 * includes it so the whole audio API reaches sketches through the `<BLE.h>`
 * umbrella (like `stream/BLEStream.h` and `l2cap/BLEL2CAP.h`). It must stay
 * backend-agnostic -- no `esp_ble_audio_*` types in this header (enforced by
 * tests/check_backend_isolation.sh).
 *
 * Lifecycle:
 *   BLE.begin("Headset");
 *   auto audio = BLE.getAudioController();  // handle to the single engine
 *   audio.setPresentationDelay(40000);      // global defaults, before begin()
 *   audio.begin();                          // engine common_init + event bridge
 *   // ... mint role handles, configure them ...
 *   audio.start();                          // single coordinated GATT commit
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <memory>
#include "BTStatus.h"
#include "audio/BLEAudioTypes.h"

class BLEAudioUnicastServer;
class BLEAudioUnicastClient;
class BLEAudioBroadcastSource;
class BLEAudioBroadcastSink;
class BLEAudioVolumeRenderer;
class BLEAudioVolumeController;
class BLEAudioMicDevice;
class BLEAudioMicController;
class BLEAudioCoordinatedSetMember;
class BLEAudioCoordinatedSetCoordinator;
class BLEAudioCapAcceptor;
class BLEAudioCapInitiator;
class BLEAudioCapCommander;
class BLEAudioMediaPlayer;
class BLEAudioMediaController;
class BLEAudioCallServer;
class BLEAudioCallController;
class BLEAudioTmap;
class BLEAudioGmap;
class BLEAudioHearingAidDevice;
class BLEAudioHearingAidController;

class BLEAudio {
public:
  BLEAudio();
  ~BLEAudio() = default;
  BLEAudio(const BLEAudio &) = default;
  BLEAudio &operator=(const BLEAudio &) = default;
  BLEAudio(BLEAudio &&) = default;
  BLEAudio &operator=(BLEAudio &&) = default;

  /**
   * @brief Check whether this handle references the live audio controller.
   * @return true if backed by an allocated implementation (i.e. minted after BLE.begin()).
   */
  explicit operator bool() const;

  // --- Lifecycle ---

  /**
   * @brief Initialize the LE Audio engine (GAP/GATT init, event bridge, security).
   *
   * Performs the engine `common_init` and registers the audio component as a
   * contributor/commit-owner with the core GATT coordinator. Global defaults
   * (see setters below) must be set before calling this. Does NOT commit the
   * GATT table -- that happens in start().
   *
   * @return BTStatus::OK on success, or an error code.
   */
  BTStatus begin();

  /**
   * @brief Commit the audio (and any coexisting classic) GATT services.
   *
   * Triggers the single coordinated `ble_gatts_start()` through the GATT
   * coordinator, exposing every staged service (audio profiles + any
   * `BLEServer` custom services) at once. Call after minting and configuring
   * role handles.
   *
   * @return BTStatus::OK on success, or an error code.
   */
  BTStatus start();

  /**
   * @brief Shut down the LE Audio engine and release audio resources.
   *
   * @note Does not shut down the host stack; call BLE.end() for that.
   */
  void end();

  /**
   * @brief Whether begin() has completed and end() has not been called.
   */
  bool isActive() const;

  // --- Global defaults (set before begin()) ---

  /**
   * @brief Set the default presentation delay applied to streams that don't override it.
   * @param delayUs Presentation delay in microseconds (spec range, e.g. 20000-40000).
   * @return The same handle, for fluent chaining.
   */
  BLEAudio &setPresentationDelay(uint32_t delayUs);

  /**
   * @brief Get the configured default presentation delay.
   * @return Presentation delay in microseconds.
   */
  uint32_t getPresentationDelay() const;

  // --- Role factories (call after begin(), before start()) ---

  /**
   * @brief Mint a BAP Unicast Server (acceptor / peripheral) role handle.
   *
   * Configure it (capabilities/contexts, stream callbacks), then call start()
   * on the controller to commit PACS/ASCS into the coordinated GATT table.
   * @return A unicast server handle bound to this controller.
   */
  BLEAudioUnicastServer createUnicastServer();

  /**
   * @brief Mint a BAP Unicast Client (initiator / central) role handle.
   *
   * After the controller start() and an ACL connection to a unicast server,
   * call `connect()` on the returned handle to run stream setup.
   * @return A unicast client handle bound to this controller.
   */
  BLEAudioUnicastClient createUnicastClient();

  /**
   * @brief Mint a BAP Broadcast Source (Auracast transmitter) role handle.
   *
   * Configure it (preset, broadcast id/code, name), call start() on the
   * controller to create the source + BASE, then start() on the returned handle
   * to bring up the advertising carrier and begin streaming.
   * @return A broadcast source handle bound to this controller.
   */
  BLEAudioBroadcastSource createBroadcastSource();

  /**
   * @brief Mint a BAP Broadcast Sink (Auracast receiver) role handle.
   *
   * Configure it (preset, broadcast code, target name/id), call start() on the
   * controller to register PACS + the Scan Delegator (BASS), then start() on the
   * returned handle to scan and auto-sync to a matching source.
   * @return A broadcast sink handle bound to this controller.
   */
  BLEAudioBroadcastSink createBroadcastSink();

  // --- Control-profile role factories (call after begin(), before start()) ---

  /**
   * @brief Mint a Volume Control Profile Renderer (VCP server) role handle.
   *
   * Publishes the Volume Control Service (with the compiled-in VOCS/AICS
   * sub-services). Configure its initial state, then call start() on the
   * controller to commit VCS into the coordinated GATT table.
   * @return A volume renderer handle bound to this controller.
   */
  BLEAudioVolumeRenderer createVolumeRenderer();

  /**
   * @brief Mint a Volume Control Profile Controller (VCP client) role handle.
   *
   * After the controller start() and an ACL connection to a renderer, call
   * `discover()` on the returned handle to drive the peer's volume/mute.
   * @return A volume controller handle bound to this controller.
   */
  BLEAudioVolumeController createVolumeController();

  /**
   * @brief Mint a Microphone Control Profile Device (MICP server) role handle.
   *
   * Publishes the Microphone Control Service (with its AICS sub-service).
   * Configure its initial state, then call start() on the controller to commit
   * MICS into the coordinated GATT table.
   * @return A microphone device handle bound to this controller.
   */
  BLEAudioMicDevice createMicDevice();

  /**
   * @brief Mint a Microphone Control Profile Controller (MICP client) role handle.
   *
   * After the controller start() and an ACL connection to a device, call
   * `discover()` on the returned handle to drive the peer's mute state.
   * @return A microphone controller handle bound to this controller.
   */
  BLEAudioMicController createMicController();

  /**
   * @brief Mint a CSIP Set Member (server) role handle.
   *
   * Publishes the Coordinated Set Identification Service (SIRK/size/rank).
   * Configure it, then call start() on the controller to commit CSIS into the
   * coordinated GATT table.
   * @return A coordinated-set member handle bound to this controller.
   */
  BLEAudioCoordinatedSetMember createCoordinatedSetMember();

  /**
   * @brief Mint a CSIP Set Coordinator (client) role handle.
   *
   * After the controller start() and an ACL connection to a set member, call
   * `discover()` on the returned handle to read the peer's set info.
   * @return A coordinated-set coordinator handle bound to this controller.
   */
  BLEAudioCoordinatedSetCoordinator createCoordinatedSetCoordinator();

  /**
   * @brief Mint a CAP Acceptor (server) role handle.
   *
   * Publishes the Common Audio Service (CAS) with an included CSIS, making the
   * device a spec-compliant CAP acceptor. Pair with an audio data-plane role
   * (unicast server / broadcast sink).
   * @return A CAP acceptor handle bound to this controller.
   */
  BLEAudioCapAcceptor createCapAcceptor();

  /**
   * @brief Mint a CAP Initiator (client) role handle.
   *
   * After the controller start() and an ACL connection, call `discover()` to
   * verify the peer supports CAP before coordinating unicast streams.
   * @return A CAP initiator handle bound to this controller.
   */
  BLEAudioCapInitiator createCapInitiator();

  /**
   * @brief Mint a CAP Commander (client) role handle.
   *
   * After the controller start() and an ACL connection, use it to apply
   * coordinated volume / mute changes to a connected acceptor.
   * @return A CAP commander handle bound to this controller.
   */
  BLEAudioCapCommander createCapCommander();

  /**
   * @brief Mint an MCP Media Player (server) role handle.
   *
   * Publishes the engine's turnkey reference media player as a Media Control
   * Service (MCS) so a controller can drive playback.
   * @return A media player handle bound to this controller.
   */
  BLEAudioMediaPlayer createMediaPlayer();

  /**
   * @brief Mint an MCP Media Controller (client) role handle.
   *
   * After the controller start() and an ACL connection, call `discover()` then
   * drive the peer's playback (play/pause/stop/...).
   * @return A media controller handle bound to this controller.
   */
  BLEAudioMediaController createMediaController();

  /**
   * @brief Mint a CCP Call Server (GTBS) role handle.
   *
   * Publishes a Generic Telephone Bearer so a controller can observe and drive
   * calls. Announce incoming calls with `incomingCall()`.
   * @return A call server handle bound to this controller.
   */
  BLEAudioCallServer createCallServer();

  /**
   * @brief Mint a CCP Call Controller (client) role handle.
   *
   * After the controller start() and an ACL connection, call `discover()` then
   * originate / accept / terminate calls on the peer.
   * @return A call controller handle bound to this controller.
   */
  BLEAudioCallController createCallController();

  // --- Top-level profile identity factories (call after begin(), before start()) ---

  /**
   * @brief Mint a TMAP (Telephony and Media Audio Profile) identity handle.
   *
   * Set the local role(s) with `setRoles()` before start() to publish a TMAS
   * instance; leave them unset to use it purely as a client that discovers a
   * peer's TMAP roles. The audio itself still flows through CAP + BAP + the
   * control profiles.
   * @return A TMAP handle bound to this controller.
   */
  BLEAudioTmap createTmap();

  /**
   * @brief Mint a GMAP (Gaming Audio Profile) identity handle.
   *
   * Server publish (GMAS) is **stubbed** on packaged `release/v6.1` libs — see
   * `BLEAudioGmap` / `AUDIO.md`. Leave roles unset to use the handle as a
   * client that discovers a peer's GMAP roles when the peer exposes GMAS.
   * @return A GMAP handle bound to this controller.
   */
  BLEAudioGmap createGmap();

  /**
   * @brief Mint a HAS hearing-aid device (server) role handle.
   *
   * Publishes a Hearing Access Service exposing named presets. Add presets and
   * configure the type before start().
   * @return A hearing-aid device handle bound to this controller.
   */
  BLEAudioHearingAidDevice createHearingAidDevice();

  /**
   * @brief Mint a HAS hearing-aid controller (client) role handle.
   *
   * After the controller start() and an ACL connection, call `discover()` then
   * read/switch the peer's presets.
   * @return A hearing-aid controller handle bound to this controller.
   */
  BLEAudioHearingAidController createHearingAidController();

  struct Impl;

private:
  explicit BLEAudio(std::shared_ptr<Impl> impl) : _impl(std::move(impl)) {}
  std::shared_ptr<Impl> _impl;

  friend class BLEClass;
};

// Audio component aggregator: pull the value types + role handles in so the
// whole audio API reaches sketches through the <BLE.h> umbrella.
#include "audio/BLEAudioStream.h"
#include "audio/BLEAudioUnicastServer.h"
#include "audio/BLEAudioUnicastClient.h"
#include "audio/BLEAudioBroadcastSource.h"
#include "audio/BLEAudioBroadcastSink.h"

// Control profiles (Phase 4).
#include "audio/BLEAudioVolume.h"
#include "audio/BLEAudioMic.h"
#include "audio/BLEAudioCoordinatedSet.h"
#include "audio/BLEAudioCap.h"
#include "audio/BLEAudioMedia.h"
#include "audio/BLEAudioCall.h"

// Top-level profile identity facades (Phase 5).
#include "audio/BLEAudioProfiles.h"
#include "audio/BLEAudioHearingAid.h"

// Turnkey LC3 data-plane facades (self-guard on BLE_AUDIO_LC3_SUPPORTED, i.e.
// the esp_audio_codec managed component being compiled in).
#include "audio/BLEAudioPlayer.h"
#include "audio/BLEAudioRecorder.h"

#endif /* BLE_AUDIO_SUPPORTED */
