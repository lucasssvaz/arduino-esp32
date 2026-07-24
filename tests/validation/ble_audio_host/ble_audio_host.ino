// LE Audio host-interop validation — DEVICE UNDER TEST (single DUT)
//
// Counterpart: a Linux host driving Bumble on an LE-Audio-capable USB dongle
// (see README). The ESP is the peripheral/acceptor and Auracast source; the
// Linux host is the central/peer. pytest (test_ble_audio_host.py) runs on the
// same Linux host, drives the dongle exclusively via Bumble (GATT + CIS/BIS +
// LC3), and cross-checks the DUT's serial output. BlueZ must not own the HCI.
//
// The suite is phase-driven over serial: the host sends START_PHASE_<n>, the
// DUT configures the roles that phase needs and advertises/broadcasts, then
// stays in that phase until the host advances it with START_PHASE_<n+1>.
// Control operations the host performs are echoed on the DUT's serial as
// "[DUT] <event>" lines so the host can assert them landed on the device.
//
// TEMPORARY — BLE_AUDIO_HOST_PHASE_SOFT_REBOOT (keep in sync with
// test_ble_audio_host.py::PHASE_SOFT_REBOOT): packaged esp_ble_audio has no
// common_deinit, so a second audio.begin() in one boot hits profile AlreadyInit
// (LibAicsInitFail). With the flag on, each phase is one boot; the DUT
// ESP.restart()s after phases 1–5 and the host re-handshakes the name. When IDF
// ships proper deinit, set both flags to 0 and delete the soft-reboot branches.
//
// Phases:
//   1 control_gatt          full CAP acceptor exposing every control profile
//                           (VCP/MICP/MCP/CCP/CSIP/CAS/HAS/TMAP/GMAP) plus a
//                           BAP unicast server (PACS/ASCS), all committed into
//                           one GATT table.
//   2 broadcast_announcement Auracast Broadcast Source announcing a BASE the
//                           host scans/parses (transport only, raw SDUs).
//   3 unicast_audio         BAP unicast server with a real LC3 data plane:
//                           BLEAudioPlayer on the sink ASE (decodes what the
//                           host streams) and BLEAudioRecorder on the source
//                           ASE (streams a 1 kHz tone the host records).
//   4 broadcast_audio       Auracast Broadcast Source streaming a real LC3 tone
//                           the host syncs to and records.
//   5 scan_delegator        Broadcast sink / Scan Delegator exposing BASS
//                           (Broadcast Audio Scan Service) for the host to
//                           discover and drive over GATT. Its own phase because
//                           it owns the single PACS registration (cannot coexist
//                           with the unicast server in control_gatt).
//   6 memory_release        teardown + reinit guard.
//
// Every phase self-skips when the relevant feature is not compiled in, so the
// sketch still builds/runs on non-audio silicon (it just reports "not
// supported" and the host records the phase as skipped).

#include <Arduino.h>
#include <BLE.h>
#include "esp_heap_caps.h"
#if defined(CONFIG_BT_NIMBLE_ENABLED)
#include <host/ble_gatt.h>
#include <host/ble_uuid.h>
#endif

// Set to 0 when esp_ble_audio_common_deinit (+ profile AlreadyInit teardown) is
// available in packaged libs. Must match test_ble_audio_host.py::PHASE_SOFT_REBOOT.
#ifndef BLE_AUDIO_HOST_PHASE_SOFT_REBOOT
#define BLE_AUDIO_HOST_PHASE_SOFT_REBOOT 1
#endif

static const BLEUUID COEX_SVC_UUID("a10c0001-0000-1000-8000-00805f9b34fb");
static const BLEUUID COEX_CHAR_UUID("a10c0002-0000-1000-8000-00805f9b34fb");

String dutName;
volatile int currentPhase = 0;

// Control-plane observation counters (written from role callbacks on the host
// task, mirrored to serial as "[DUT] ..." event lines for the host to assert).
volatile uint32_t vcpWrites = 0;
volatile uint32_t micMutes = 0;
volatile uint32_t callOriginates = 0;
volatile uint32_t hasSelects = 0;

// LC3 unicast sink decode metrics (phase 3) — written from the player PCM sink.
volatile uint32_t lc3RxSamples = 0;
volatile uint64_t lc3RxEnergy = 0;

// Broadcast sink SDU counters (phase 5) — phone or any Auracast source.
volatile uint32_t bcastRxSdus = 0;
volatile bool bcastSinkStreaming = false;

// Set when the host sends "REPORT"; a data-plane phase emits its current result
// line on demand so the host can assert it without ending the phase.
volatile bool reportRequested = false;

// ========================= Phase coordination ================================

void checkSerial() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      buf.trim();
      if (buf.startsWith("START_PHASE_")) {
        int phase = buf.substring(12).toInt();
        if (phase > currentPhase) {
          currentPhase = phase;
          Serial.printf("[DUT] Phase %d started\n", phase);
        }
      } else if (buf == "REPORT") {
        reportRequested = true;
      }
      buf = "";
    } else if (c != '\r') {
      buf += c;
    }
  }
}

// BLE callbacks run on the stack task and may preempt loop(); drain UART first
// so START_PHASE_* is applied before phase-dependent logs (keeps host + pexpect
// output ordered, same contract as the ESP-to-ESP suites).
static void syncPhaseFromHost() {
  checkSerial();
}

void waitForPhase(int n) {
  while (currentPhase < n) {
    checkSerial();
    delay(10);
  }
}

// Stay in phase n, servicing serial + async callbacks, until the host advances
// to the next phase (or the optional safety deadline elapses).
void runPhaseUntilNext(int n, unsigned long maxMs) {
  unsigned long deadline = millis() + maxMs;
  while (currentPhase == n && millis() < deadline) {
    checkSerial();
    delay(20);
  }
}

void readName() {
  Serial.println("[DUT] Device ready for name");
  Serial.println("[DUT] Send name:");
  while (dutName.length() == 0) {
    if (Serial.available()) {
      dutName = Serial.readStringUntil('\n');
      dutName.trim();
    }
    delay(100);
  }
  Serial.printf("[DUT] Name: %s\n", dutName.c_str());
}

static void advertiseConnectable() {
  BLEAdvertising adv = BLE.getAdvertising();
  adv.reset();
  adv.setType(BLEAdvType::ConnectableScannable);
  adv.setName(dutName);
  adv.start();
}

// Unicast / scan-delegator: advertise ASCS + PACS UUIDs alongside the name so
// a BAP client can identify the unicast server before GATT discovery.
static void advertiseLeAudioServer() {
  BLEAdvertising adv = BLE.getAdvertising();
  adv.reset();
  adv.setType(BLEAdvType::ConnectableScannable);
  adv.setName(dutName);
  adv.setAppearance(0x0840);                      // Generic Audio Sink
  adv.addServiceUUID(BLEUUID((uint16_t)0x184F));  // BASS (Scan Delegator)
  adv.addServiceUUID(BLEUUID((uint16_t)0x184E));  // ASCS
  adv.addServiceUUID(BLEUUID((uint16_t)0x1850));  // PACS
  adv.start();
  BLE.createServer().advertiseOnDisconnect(true);
}

// Report which audio services actually reached the local GATT table, and at
// which handle. A service the peer cannot discover but that is present here was
// committed and not exported; one missing here was never registered at all.
// CAS in particular carries no characteristics, so this is the only way to tell
// those two cases apart.
static void reportLocalSvcHandles() {
#if defined(CONFIG_BT_NIMBLE_ENABLED)
  struct {
    const char *name;
    uint16_t uuid;
  } svcs[] = {
    {"PACS", 0x1850}, {"ASCS", 0x184E}, {"VCS", 0x1844},  {"MICS", 0x184D}, {"GMCS", 0x1849}, {"GTBS", 0x184C},
    {"HAS", 0x1854},  {"TMAS", 0x1855}, {"CSIS", 0x1846}, {"CAS", 0x1853},  {"GMAS", 0x1858},
  };
  for (auto &s : svcs) {
    uint16_t handle = 0;
    ble_uuid16_t uuid = BLE_UUID16_INIT(0);
    uuid.value = s.uuid;
    int rc = ble_gatts_find_svc((const ble_uuid_t *)&uuid, &handle);
    Serial.printf("[DUT] localsvc %s uuid=0x%04x rc=%d handle=%u\n", s.name, s.uuid, rc, (unsigned)handle);
  }
#endif
}

// ========================= Phase 1 — control_gatt ============================

#if BLE_AUDIO_SUPPORTED
static void phaseControlGatt() {
  BLE.end(false);
  delay(500);
  vcpWrites = 0;
  micMutes = 0;
  callOriginates = 0;
  hasSelects = 0;

  BTStatus s = BLE.begin(dutName);
  if (!s) {
    Serial.printf("[DUT] ControlGatt init FAILED: %s\n", s.toString());
    return;
  }
  BLEAudio audio = BLE.getAudioController();
  BTStatus ab = audio ? audio.begin() : BTStatus::InvalidState;
  if (!ab) {
    Serial.printf("[DUT] ControlGatt engine begin FAILED: %s\n", ab.toString());
    return;
  }

  // Authorization defaults to deny, which surfaces on the host as ATT 0x08
  // (Insufficient Authorization). Accept and log so any attribute that reaches
  // this hook is identified by handle rather than only by its host-side error.
  BLESecurity sec = BLE.getSecurity();
  sec.onAuthorization([](const BLEConnInfo &conn, uint16_t attrHandle, bool isRead) -> bool {
    (void)conn;
    Serial.printf("[DUT] AUTHORIZE attr=%u read=%d -> ACCEPT\n", (unsigned)attrHandle, isRead ? 1 : 0);
    return true;
  });

  // A plain custom service alongside the audio profiles proves classic+audio
  // coexistence in one committed GATT table (the host discovers it too).
  BLEServer srv = BLE.createServer();
  BLEService coex = srv.createService(COEX_SVC_UUID);
  BLECharacteristic coexChr = coex.createCharacteristic(COEX_CHAR_UUID, BLEProperty::Read | BLEProperty::Notify, BLEPermissions::OpenRead);
  coexChr.setValue("coexist");
  srv.start();

  // CAP acceptor (CAS + included CSIS) — the coordination umbrella.
  BLEAudioCapAcceptor cap = audio.createCapAcceptor();
  cap.setSetSize(2).setRank(1);

  // BAP unicast server (PACS/ASCS) so the host sees a full acceptor. Note: the
  // unicast server owns the single PACS registration, so a broadcast sink (which
  // also registers PACS) cannot coexist here — BASS is covered in its own
  // scan_delegator phase instead.
  BLEAudioUnicastServer us = audio.createUnicastServer();
  us.enableSink(true)
    .enableSource(true)
    .setSinkLocation(BLEAudioLocation::Mono)
    .setSourceLocation(BLEAudioLocation::Mono)
    .setSinkContexts(BLEAudioContext::Media | BLEAudioContext::Conversational)
    .setSourceContexts(BLEAudioContext::Conversational);

  // VCP renderer (VCS).
  BLEAudioVolumeRenderer vol = audio.createVolumeRenderer();
  vol.setInitialVolume(100);
  vol.onStateChanged([](uint8_t volume, bool muted) {
    vcpWrites = vcpWrites + 1;
    Serial.printf("[DUT] VCP state vol=%u muted=%d\n", (unsigned)volume, muted ? 1 : 0);
  });

  // MICP device (MICS).
  BLEAudioMicDevice mic = audio.createMicDevice();
  mic.onMuteChanged([](bool muted) {
    micMutes = micMutes + 1;
    Serial.printf("[DUT] MICP mute=%d\n", muted ? 1 : 0);
  });

  // MCP reference media player (MCS/GMCS).
  BLEAudioMediaPlayer media = audio.createMediaPlayer();
  (void)media;

  // CCP call server (GTBS).
  BLEAudioCallServer call = audio.createCallServer();
  call.setProviderName("ESP Phone").setUci("un000");
  // GTBS answers Originate with Invalid Outgoing URI because it has no
  // telephone bearer to route the call to, so this never fires; it stays wired
  // to prove the request is rejected by the bearer and not silently accepted.
  call.onOriginate([](uint8_t callIndex, const std::string &uri) -> bool {
    (void)callIndex;
    callOriginates = callOriginates + 1;
    Serial.printf("[DUT] CCP originate uri=%s\n", uri.c_str());
    return true;
  });
  call.onTerminated([](uint8_t callIndex, uint8_t reason) {
    Serial.printf("[DUT] CCP terminated idx=%u reason=%u\n", (unsigned)callIndex, (unsigned)reason);
  });

  // HAS hearing-aid device with a small preset list.
  BLEAudioHearingAidDevice ha = audio.createHearingAidDevice();
  ha.setType(BLEHearingAidType::Binaural).setPresetSync(false).addPreset(1, "Universal").addPreset(2, "Outdoor").addPreset(3, "Restaurant");
  ha.onPresetSelected([](uint8_t index, bool sync) {
    (void)sync;
    hasSelects = hasSelects + 1;
    Serial.printf("[DUT] HAS preset select idx=%u\n", (unsigned)index);
  });

  // TMAP + GMAP identity.
  BLEAudioTmap tmap = audio.createTmap();
  tmap.setRoles(BLEAudioTmapRole::CallTerminal | BLEAudioTmapRole::UnicastMediaReceiver);
  BLEAudioGmap gmap = audio.createGmap();
  gmap.setRoles(BLEAudioGmapRole::UnicastGameTerminal).setFeatures(0, 0x04 /* UGT SINK */, 0, 0);

  if (!audio.start()) {
    Serial.println("[DUT] ControlGatt start FAILED");
    audio.end();
    return;
  }
  ha.setActivePreset(1);

  advertiseConnectable();

  // Announce an incoming call so the host can observe live GTBS call state.
  uint8_t callIndex = 0;
  (void)call.incomingCall("tel:+15551234567", callIndex);

  Serial.println("[DUT] ControlGatt ready");
  // Printed after the ready marker so the host can consume it: the host waits on
  // that marker, which discards anything logged before it.
  reportLocalSvcHandles();
  runPhaseUntilNext(1, 180000);

  Serial.printf(
    "[DUT] ControlGatt result vcp=%lu mic=%lu call=%lu has=%lu\n", (unsigned long)vcpWrites, (unsigned long)micMutes, (unsigned long)callOriginates,
    (unsigned long)hasSelects
  );
  audio.end();
  // Let the engine/controller free ISO + ACL resources before controller
  // deinit, otherwise esp_bt_controller_deinit can assert on a non-empty event
  // mempool (btdm_osal_elem_mempool_deinit).
  delay(1000);
  BLE.end(false);
  delay(500);
}
#endif

// ========================= Phase 2 — broadcast_announcement ==================

#if BLE_AUDIO_SUPPORTED
static void phaseBroadcastAnnouncement() {
  BLE.end(false);
  delay(500);
  BTStatus s = BLE.begin(dutName);
  if (!s) {
    Serial.printf("[DUT] BcastAnnounce init FAILED: %s\n", s.toString());
    return;
  }
  BLEAudio audio = BLE.getAudioController();
  BTStatus ab = audio ? audio.begin() : BTStatus::InvalidState;
  if (!ab) {
    Serial.printf("[DUT] BcastAnnounce engine begin FAILED: %s\n", ab.toString());
    return;
  }
  BLEAudioBroadcastSource source = audio.createBroadcastSource();
  source.setPreset(BLEAudioCodecPreset::LC3_16_2_1).setBroadcastId(0x123456).setName(dutName);
  BLEAudioStream src = source.sourceStream();

  bool streaming = false;
  src.onStarted([&streaming](BLEAudioStream &) {
    streaming = true;
  });
  src.onStopped([&streaming](BLEAudioStream &, uint8_t) {
    streaming = false;
  });

  if (audio.start() && source.start()) {
    Serial.println("[DUT] BcastAnnounce ready id=0x123456");
    // Announcement-only: do not pump ISO SDUs. source.start() still opens a
    // BIG, so ISO_SHIM ChanCannotSend may still appear; the crash on advance
    // is from source.stop()/audio.end() on that wedged BIG (null ISO queue).
    while (currentPhase == 2) {
      syncPhaseFromHost();
      delay(50);
    }
#if BLE_AUDIO_HOST_PHASE_SOFT_REBOOT
    // Soft-reboot is imminent — skip ISO teardown; ESP.restart() wipes state.
    return;
#else
    streaming = false;
    delay(50);
    source.stop();
    delay(200);
#endif
  } else {
    Serial.println("[DUT] BcastAnnounce start FAILED");
  }
#if !BLE_AUDIO_HOST_PHASE_SOFT_REBOOT
  audio.end();
  // Let the engine/controller free ISO + ACL resources before controller
  // deinit, otherwise esp_bt_controller_deinit can assert on a non-empty event
  // mempool (btdm_osal_elem_mempool_deinit).
  delay(1000);
  BLE.end(false);
  delay(500);
#endif
}
#endif

// ========================= Phase 3 — unicast_audio ===========================

#if BLE_AUDIO_LC3_SUPPORTED
static void phaseUnicastAudio() {
  BLE.end(false);
  delay(500);
  lc3RxSamples = 0;
  lc3RxEnergy = 0;
  BTStatus s = BLE.begin(dutName);
  if (!s) {
    Serial.printf("[DUT] UnicastAudio init FAILED: %s\n", s.toString());
    return;
  }
  BLEAudio audio = BLE.getAudioController();
  BTStatus ab = audio ? audio.begin() : BTStatus::InvalidState;
  if (!ab) {
    Serial.printf("[DUT] UnicastAudio engine begin FAILED: %s\n", ab.toString());
    return;
  }

  // Just Works + bonding; clear NVS bonds so a prior phase's host identity
  // (fresh random addr each Bumble session) cannot poison SMP on this boot.
  // LE Audio attrs are authorization-gated — accept so the host can drive ASCS.
  BLESecurity sec = BLE.getSecurity();
  sec.setIOCapability(BLESecurity::IOCapability::NoInputNoOutput);
  sec.setAuthenticationMode(/*bonding=*/true, /*mitm=*/false, /*secureConnection=*/true);
  (void)sec.deleteAllBonds();
  sec.onAuthorization([](const BLEConnInfo &conn, uint16_t attrHandle, bool isRead) -> bool {
    (void)conn;
    Serial.printf("[DUT] AUTHORIZE attr=%u read=%d -> ACCEPT\n", (unsigned)attrHandle, isRead ? 1 : 0);
    return true;
  });

  BLEAudioUnicastServer us = audio.createUnicastServer();
  us.enableSink(true)
    .enableSource(true)
    .setSinkLocation(BLEAudioLocation::Mono)
    .setSourceLocation(BLEAudioLocation::Mono)
    .setSinkContexts(BLEAudioContext::Media | BLEAudioContext::Conversational)
    .setSourceContexts(BLEAudioContext::Conversational);
  BLEAudioStream sink = us.sinkStream();
  BLEAudioStream source = us.sourceStream();

  if (!audio.start()) {
    Serial.println("[DUT] UnicastAudio start FAILED");
    audio.end();
    return;
  }

  // Sink: decode whatever the host streams and accumulate sample count + energy
  // so the host can assert real audio arrived (mirrors the ESP-to-ESP loopback).
  BLEAudioPlayer player(
    sink,
    [](const int16_t *pcm, size_t n) {
      lc3RxSamples = lc3RxSamples + (uint32_t)n;
      uint64_t e = 0;
      for (size_t i = 0; i < n; i++) {
        int v = pcm[i];
        e += (v < 0) ? (uint64_t)(-v) : (uint64_t)v;
      }
      lc3RxEnergy = lc3RxEnergy + e;
    },
    BLEAudioCodecPreset::LC3_16_2_1
  );

  // Source: stream a deterministic 1 kHz tone (16 samples/cycle at 16 kHz) the
  // host records and analyzes.
  BLEAudioRecorder recorder(
    source,
    [](int16_t *pcm, size_t maxSamples) -> size_t {
      static uint32_t ph = 0;
      for (size_t i = 0; i < maxSamples; i++) {
        float v = sinf((float)ph * 2.0f * 3.14159265f / 16.0f);
        pcm[i] = (int16_t)(v * 8000.0f);
        ph++;
      }
      return maxSamples;
    },
    BLEAudioCodecPreset::LC3_16_2_1
  );

  bool playerOk = (bool)player.begin();
  bool recOk = (bool)recorder.begin();
  advertiseLeAudioServer();
  Serial.printf("[DUT] UnicastAudio ready player=%d recorder=%d\n", playerOk ? 1 : 0, recOk ? 1 : 0);

  // Stay up until the host advances; emit the live decode metric whenever the
  // host asks (REPORT) so it can assert the audio it streamed was decoded.
  while (currentPhase == 3) {
    checkSerial();
    if (reportRequested) {
      reportRequested = false;
      unsigned long long a = lc3RxSamples ? (unsigned long long)(lc3RxEnergy / lc3RxSamples) : 0ull;
      Serial.printf("[DUT] UnicastAudio result samples=%lu meanamp=%llu\n", (unsigned long)lc3RxSamples, a);
    }
    delay(20);
  }

  recorder.end();
  player.end();
  unsigned long long avg = lc3RxSamples ? (unsigned long long)(lc3RxEnergy / lc3RxSamples) : 0ull;
  Serial.printf("[DUT] UnicastAudio final samples=%lu meanamp=%llu\n", (unsigned long)lc3RxSamples, avg);
  audio.end();
  // Let the engine/controller free ISO + ACL resources before controller
  // deinit, otherwise esp_bt_controller_deinit can assert on a non-empty event
  // mempool (btdm_osal_elem_mempool_deinit).
  delay(1000);
  BLE.end(false);
  delay(500);
}
#endif

// ========================= Phase 4 — broadcast_audio =========================

#if BLE_AUDIO_LC3_SUPPORTED
static void phaseBroadcastAudio() {
  BLE.end(false);
  delay(500);
  BTStatus s = BLE.begin(dutName);
  if (!s) {
    Serial.printf("[DUT] BcastAudio init FAILED: %s\n", s.toString());
    return;
  }
  BLEAudio audio = BLE.getAudioController();
  BTStatus ab = audio ? audio.begin() : BTStatus::InvalidState;
  if (!ab) {
    Serial.printf("[DUT] BcastAudio engine begin FAILED: %s\n", ab.toString());
    return;
  }
  BLEAudioBroadcastSource source = audio.createBroadcastSource();
  // 48 kHz HQ matches typical Galaxy Auracast Listen; 16 kHz SQ often shows as
  // "Unknown" / ignores taps even when the announcement is visible.
  source.setPreset(BLEAudioCodecPreset::LC3_48_4_1).setBroadcastId(0x123456).setName(dutName);
  BLEAudioStream src = source.sourceStream();

  if (!audio.start() || !source.start()) {
    Serial.println("[DUT] BcastAudio start FAILED");
    audio.end();
    return;
  }

  // Encode a real 1 kHz LC3 tone onto the BIS via the turnkey recorder.
  BLEAudioRecorder recorder(
    src,
    [](int16_t *pcm, size_t maxSamples) -> size_t {
      static uint32_t ph = 0;
      for (size_t i = 0; i < maxSamples; i++) {
        float v = sinf((float)ph * 2.0f * 3.14159265f / 48.0f);
        pcm[i] = (int16_t)(v * 8000.0f);
        ph++;
      }
      return maxSamples;
    },
    BLEAudioCodecPreset::LC3_48_4_1
  );
  bool recOk = (bool)recorder.begin();
  Serial.printf("[DUT] BcastAudio ready recorder=%d id=0x123456 name=%s\n", recOk ? 1 : 0, dutName.c_str());
  Serial.println("[DUT] PHONE_SOURCE: join this Auracast (Listen) — 1 kHz tone, no password");

  while (currentPhase == 4) {
    checkSerial();
    if (reportRequested) {
      reportRequested = false;
      Serial.printf("[DUT] BcastAudio result recorder=%d\n", recOk ? 1 : 0);
    }
    delay(20);
  }

#if BLE_AUDIO_HOST_PHASE_SOFT_REBOOT
  // Soft-reboot is imminent — skip ISO teardown; ESP.restart() wipes state.
  return;
#else
  recorder.end();
  source.stop();
  Serial.println("[DUT] BcastAudio final done");
  audio.end();
  // Let the engine/controller free ISO + ACL resources before controller
  // deinit, otherwise esp_bt_controller_deinit can assert on a non-empty event
  // mempool (btdm_osal_elem_mempool_deinit).
  delay(1000);
  BLE.end(false);
  delay(500);
#endif
}
#endif

// ========================= Phase 5 — scan_delegator (BASS) ===================

#if BLE_AUDIO_SUPPORTED
static void phaseScanDelegator() {
  BLE.end(false);
  delay(500);
  BTStatus s = BLE.begin(dutName);
  if (!s) {
    Serial.printf("[DUT] ScanDelegator init FAILED: %s\n", s.toString());
    return;
  }
  BLEAudio audio = BLE.getAudioController();
  BTStatus ab = audio ? audio.begin() : BTStatus::InvalidState;
  if (!ab) {
    Serial.printf("[DUT] ScanDelegator engine begin FAILED: %s\n", ab.toString());
    return;
  }

  // LE Audio attrs are authorization-gated by default; accept so the host's
  // paired Bumble client can read/write BASS (same contract as control_gatt).
  BLESecurity sec = BLE.getSecurity();
  sec.onAuthorization([](const BLEConnInfo &conn, uint16_t attrHandle, bool isRead) -> bool {
    (void)conn;
    Serial.printf("[DUT] AUTHORIZE attr=%u read=%d -> ACCEPT\n", (unsigned)attrHandle, isRead ? 1 : 0);
    return true;
  });

  // Broadcast sink acts as the Scan Delegator; committing it publishes BASS
  // (+ PACS). No unicast server here, so PACS is registered exactly once.
  // Also self-syncs to a nearby Auracast source (phone) so the host can assert
  // BIS SDUs without needing iso-broadcaster on the Linux HCI.
  bcastRxSdus = 0;
  BLEAudioBroadcastSink bsink = audio.createBroadcastSink();
  BLEAudioStream rx = bsink.sinkStream();
  rx.onStarted([](BLEAudioStream &) { bcastSinkStreaming = true; });
  rx.onStopped([](BLEAudioStream &, uint8_t) { bcastSinkStreaming = false; });
  rx.onReceive([](BLEAudioStream &, const BLEAudioSduInfo &info, const uint8_t *, uint16_t) {
    if (info.packetStatus == 0) {
      bcastRxSdus = bcastRxSdus + 1;
    }
  });

  if (!audio.start()) {
    Serial.println("[DUT] ScanDelegator start FAILED");
    audio.end();
    return;
  }
  advertiseLeAudioServer();
  bool sinkScan = (bool)bsink.start();
  Serial.printf("[DUT] ScanDelegator ready sink_scan=%d\n", sinkScan ? 1 : 0);
  Serial.println("[DUT] PHONE_SINK: connect the phone to this name, then Broadcast using Auracast (no password)");

  while (currentPhase == 5) {
    checkSerial();
    if (reportRequested) {
      reportRequested = false;
      Serial.printf("[DUT] BcastSink result received=%lu streaming=%d\n", (unsigned long)bcastRxSdus,
                    bcastSinkStreaming ? 1 : 0);
    }
    delay(20);
  }

  bsink.stop();
  Serial.printf("[DUT] ScanDelegator result received=%lu\n", (unsigned long)bcastRxSdus);
  audio.end();
  // Let the engine/controller free ISO + ACL resources before controller
  // deinit, otherwise esp_bt_controller_deinit can assert on a non-empty event
  // mempool (btdm_osal_elem_mempool_deinit).
  delay(1000);
  BLE.end(false);
  delay(500);
}
#endif

// ========================= Setup / phase sequence ============================

static void runMemoryReleasePhase() {
  // Ensure a live stack so the release frees real state.
  (void)BLE.begin(dutName);
  delay(300);
  size_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[DUT] Heap before release: %u\n", (unsigned)before);
  BLE.end(true);
  delay(500);
  size_t after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[DUT] Heap after release: %u\n", (unsigned)after);
  int32_t freed = (int32_t)after - (int32_t)before;
  Serial.printf("[DUT] Memory freed: %ld bytes\n", (long)freed);
  if (freed >= 10240) {
    Serial.println("[DUT] Memory release OK");
  } else {
    Serial.println("[DUT] Memory release INSUFFICIENT");
  }
  BTStatus s = BLE.begin("ShouldFail");
  if (!s) {
    Serial.println("[DUT] Reinit blocked OK");
  } else {
    Serial.println("[DUT] Reinit was NOT blocked");
  }
  Serial.println("[DUT] All phases complete");
}

static void runPhaseBody(int n) {
  switch (n) {
    case 1:
#if BLE_AUDIO_SUPPORTED
      phaseControlGatt();
#else
      Serial.println("[DUT] ControlGatt not supported");
#endif
      Serial.println("[DUT] Phase1 control_gatt done");
      break;
    case 2:
#if BLE_AUDIO_SUPPORTED
      phaseBroadcastAnnouncement();
#else
      Serial.println("[DUT] BcastAnnounce not supported");
#endif
      Serial.println("[DUT] Phase2 broadcast_announcement done");
      break;
    case 3:
#if BLE_AUDIO_LC3_SUPPORTED
      phaseUnicastAudio();
#else
      Serial.println("[DUT] UnicastAudio not supported");
#endif
      Serial.println("[DUT] Phase3 unicast_audio done");
      break;
    case 4:
#if BLE_AUDIO_LC3_SUPPORTED
      phaseBroadcastAudio();
#else
      Serial.println("[DUT] BcastAudio not supported");
#endif
      Serial.println("[DUT] Phase4 broadcast_audio done");
      break;
    case 5:
#if BLE_AUDIO_SUPPORTED
      phaseScanDelegator();
#else
      Serial.println("[DUT] ScanDelegator not supported");
#endif
      Serial.println("[DUT] Phase5 scan_delegator done");
      break;
    case 6:
      runMemoryReleasePhase();
      break;
    default:
      Serial.printf("[DUT] Unknown phase %d\n", n);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(100);
  }
  readName();

  // Baseline stack so each phase's BLE.end(false)/begin() cycle has a live
  // stack to tear down (each phase re-begins its own stack anyway).
  (void)BLE.begin(dutName);

#if BLE_AUDIO_HOST_PHASE_SOFT_REBOOT
  // One audio phase per boot — remove this branch when packaged IDF can
  // re-enter audio.begin() after end() (common_deinit). Host sets
  // PHASE_SOFT_REBOOT to match.
  while (currentPhase == 0) {
    checkSerial();
    delay(10);
  }
  const int n = currentPhase;
  runPhaseBody(n);
  if (n < 6) {
    // Stay up until the host advances (or retries the next phase after a
    // failure). Then soft-reboot so the next boot's audio.begin() is clean.
    while (currentPhase == n) {
      checkSerial();
      delay(10);
    }
    delay(300);
    ESP.restart();
  }
#else
  // Same-boot sequence (requires re-enterable audio.end()/begin()).
  for (int n = 1; n <= 6; ++n) {
    waitForPhase(n);
    runPhaseBody(n);
  }
#endif
}

void loop() {
  delay(1000);
}
