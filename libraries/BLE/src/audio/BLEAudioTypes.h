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
 * @brief Public, backend-agnostic value types for the LE Audio component.
 *
 * These mirror the LE Audio / Generic Audio Framework (GAF) spec values so
 * they map 1:1 to the engine enums, but they NEVER name an engine
 * (`esp_ble_audio_*`) type -- the translation to vendor types happens only in
 * the audio Impl / engine translation units. Audio-only value types live here
 * (not `types/`, which is reserved for types shared *across* components).
 */

#include "core/BLEGuards.h"
#if BLE_AUDIO_SUPPORTED

#include <cstdint>
#include <cstddef>

/**
 * @brief Result of an LE Audio operation.
 *
 * The library-wide @ref BTStatus is a compact 1-byte enum shared by every
 * component and cannot carry audio-specific reasons; audio APIs therefore
 * report their own richer status here and surface it through audio callbacks.
 */
enum class BLEAudioStatus : uint8_t {
  Ok = 0,             ///< Operation succeeded.
  Fail,               ///< Generic failure.
  InvalidState,       ///< Controller/engine not in a state that allows the call.
  InvalidParam,       ///< A caller-supplied argument was out of range.
  NotSupported,       ///< Role/feature not compiled in (see BLE_AUDIO_*_SUPPORTED).
  NoResources,        ///< Out of ISO channels, ASEs, or memory.
  NotConnected,       ///< No ACL/CIS/BIS transport for the request.
  CodecUnsupported,   ///< Requested codec configuration is not supported by the peer.
  Timeout,            ///< The operation did not complete in time.
};

/**
 * @brief Audio context types (BT Assigned Numbers, "Context Type" bitfield).
 *
 * Used to describe what a stream carries and which contexts a device
 * supports/makes available. Combine with `operator|`.
 */
enum class BLEAudioContext : uint16_t {
  Prohibited = 0x0000,
  Unspecified = 0x0001,
  Conversational = 0x0002,
  Media = 0x0004,
  Game = 0x0008,
  Instructional = 0x0010,
  VoiceAssistants = 0x0020,
  Live = 0x0040,
  SoundEffects = 0x0080,
  Notifications = 0x0100,
  Ringtone = 0x0200,
  Alerts = 0x0400,
  EmergencyAlarm = 0x0800,
};

inline BLEAudioContext operator|(BLEAudioContext a, BLEAudioContext b) {
  return static_cast<BLEAudioContext>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}
inline BLEAudioContext &operator|=(BLEAudioContext &a, BLEAudioContext b) {
  a = a | b;
  return a;
}

/**
 * @brief Audio channel allocation (BT Assigned Numbers, "Audio Location" bitfield).
 *
 * A 32-bit bitmap of speaker positions. `Mono` is the special all-zero value.
 */
enum class BLEAudioLocation : uint32_t {
  Mono = 0x00000000,
  FrontLeft = 0x00000001,
  FrontRight = 0x00000002,
  FrontCenter = 0x00000004,
  LowFreqEffects1 = 0x00000008,
  BackLeft = 0x00000010,
  BackRight = 0x00000020,
  SideLeft = 0x00000100,
  SideRight = 0x00000200,
};

inline BLEAudioLocation operator|(BLEAudioLocation a, BLEAudioLocation b) {
  return static_cast<BLEAudioLocation>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline BLEAudioLocation &operator|=(BLEAudioLocation &a, BLEAudioLocation b) {
  a = a | b;
  return a;
}

/**
 * @brief Standard BAP LC3 codec configuration presets (spec Table 4.2, "LC3_<sr>_<n>_<rc>").
 *
 * Each preset fixes sampling rate, frame duration, and octets-per-frame. The
 * engine expands the preset into the full codec-specific configuration LTV.
 */
enum class BLEAudioCodecPreset : uint8_t {
  LC3_8_1_1,   ///<  8 kHz,  7.5 ms
  LC3_8_2_1,   ///<  8 kHz, 10 ms
  LC3_16_1_1,  ///< 16 kHz,  7.5 ms
  LC3_16_2_1,  ///< 16 kHz, 10 ms (mandatory for most roles)
  LC3_24_1_1,  ///< 24 kHz,  7.5 ms
  LC3_24_2_1,  ///< 24 kHz, 10 ms
  LC3_32_1_1,  ///< 32 kHz,  7.5 ms
  LC3_32_2_1,  ///< 32 kHz, 10 ms
  LC3_441_1_1, ///< 44.1 kHz, 7.5 ms
  LC3_441_2_1, ///< 44.1 kHz, 10 ms
  LC3_48_1_1,  ///< 48 kHz,  7.5 ms
  LC3_48_2_1,  ///< 48 kHz, 10 ms
  LC3_48_3_1,  ///< 48 kHz,  7.5 ms, higher bitrate
  LC3_48_4_1,  ///< 48 kHz, 10 ms,  higher bitrate
  LC3_48_5_1,  ///< 48 kHz,  7.5 ms, highest bitrate
  LC3_48_6_1,  ///< 48 kHz, 10 ms,  highest bitrate
};

/**
 * @brief Explicit LC3 codec configuration.
 *
 * Prefer a @ref BLEAudioCodecPreset; this struct is for advanced/custom
 * configurations. All fields are in the spec's units.
 */
struct BLEAudioCodecConfig {
  uint32_t samplingRateHz = 16000;  ///< PCM sampling rate (e.g. 16000, 48000).
  uint16_t frameDurationUs = 10000; ///< 7500 or 10000 microseconds.
  uint16_t octetsPerFrame = 40;     ///< Encoded octets per LC3 frame.
  uint8_t framesPerSdu = 1;         ///< Blocks of codec frames per SDU.
  BLEAudioLocation channelAllocation = BLEAudioLocation::Mono;

  /** @brief Build a config from a standard BAP preset. */
  static BLEAudioCodecConfig fromPreset(BLEAudioCodecPreset preset);
};

/**
 * @brief Quality-of-Service parameters for an isochronous stream.
 *
 * Left at their defaults the engine derives spec-recommended values from the
 * chosen codec preset; set them explicitly only for custom links.
 */
struct BLEAudioQos {
  uint32_t sduIntervalUs = 10000;    ///< Interval between SDUs (matches frame duration).
  uint16_t maxSduSize = 40;          ///< Maximum SDU payload in octets.
  uint8_t retransmissionNumber = 2;  ///< Controller RTN.
  uint16_t maxTransportLatencyMs = 20;
  uint32_t presentationDelayUs = 40000;
  uint8_t phy = 0x02;                ///< Preferred PHY bitmask (0x02 = 2M).
  bool framed = false;               ///< Framed vs unframed ISOAL.
};

/**
 * @brief Per-SDU receive metadata handed to `onReceive` callbacks.
 */
struct BLEAudioSduInfo {
  uint32_t timestamp = 0;   ///< Controller reference timestamp of the SDU.
  uint16_t packetSeqNum = 0;
  uint8_t packetStatus = 0; ///< 0 = valid, 1 = possibly-invalid, 2 = lost.
};

#endif /* BLE_AUDIO_SUPPORTED */
