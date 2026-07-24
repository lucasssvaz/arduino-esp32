// LE Audio validation test — CLIENT
// Phases: audio lifecycle (ack), ISO CIS/BIS transport, BAP unicast/broadcast,
//         LC3 loopback, control profiles, top-level profiles, hearing aid,
//         memory release. Split out of the combined `ble` suite (see the server
//         sketch header). Each phase self-contains its own BLE.end()/begin()
//         and self-skips when the relevant feature is not compiled in.

#include <Arduino.h>
#include <BLE.h>
#include "esp_heap_caps.h"
#include "audio/BLEAudioIso.h"  // internal ISO transport (phases 2/3); self-guarded

String targetName;
BTAddress targetAddr;
volatile int currentPhase = 0;

// ISO transport state — written from the ISO host task, read from the phase code.
volatile bool isoCisClientConnected = false;
volatile bool isoBisClientSynced = false;
volatile uint32_t isoBisRxCount = 0;

// BAP unicast client state (phase 4). txStreaming latches when the source
// stream reaches Streaming (CIS up); the SDU send loop counts transmitted SDUs.
volatile bool bapTxStreaming = false;

// BAP broadcast sink RX counter (phase 5) — written from the audio host task.
volatile uint32_t bapBcastRxCount = 0;

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
          Serial.printf("[CLIENT] Phase %d started\n", phase);
        }
      }
      buf = "";
    } else if (c != '\r') {
      buf += c;
    }
  }
}

// Same ordering guarantee as server: apply START_PHASE_* before BLE callback side effects.
static void syncPhaseFromHost() {
  checkSerial();
}

void waitForPhase(int n) {
  while (currentPhase < n) {
    checkSerial();
    delay(10);
  }
}

void readName() {
  Serial.println("[CLIENT] Device ready for name");
  Serial.println("[CLIENT] Send name:");
  while (targetName.length() == 0) {
    if (Serial.available()) {
      targetName = Serial.readStringUntil('\n');
      targetName.trim();
    }
    delay(100);
  }
  Serial.printf("[CLIENT] Target: %s\n", targetName.c_str());
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(100);
  }
  readName();

  // Baseline stack so the first phase's BLE.end()/begin() cycle has a live stack
  // to tear down; also keeps `status` in scope for the memory_release phase.
  BTStatus status = BLE.begin("BLE_CLT");

  // ===== Phase 1: LE Audio lifecycle (server-driven; client just acks) =====
  // audio_lifecycle is a single-device (server) engine test: the client only
  // acknowledges the phase so the two-DUT handshake stays in lockstep.
  waitForPhase(1);
#if BLE_AUDIO_SUPPORTED
  Serial.println("[CLIENT] Audio lifecycle OK");
#else
  Serial.println("[CLIENT] Audio not supported, skipping");
#endif
  Serial.println("[CLIENT] Phase1 audio done");

  // ===== Phase 2: ISO CIS transport integrity (client = CIS Central) =====
  // Scans for the server, opens the ACL, brings up the internal ISO transport,
  // creates the CIG + CIS on that link, and streams transparent SDUs the server
  // counts. Self-skips when host ISO is not compiled in, so it only truly runs
  // on a dual-ISO (esp32s31<->esp32s31) bench. Restores a normal stack after.
  waitForPhase(2);
#if BLE_ISO_SUPPORTED
  {
    BLE.end(false);
    delay(500);
    isoCisClientConnected = false;
    BTStatus st = BLE.begin("BLE_CLT_ISO");
    if (st) {
      BLEScan scn = BLE.getScan();
      scn.resetCallbacks();
      scn.clearResults();
      scn.setActiveScan(true);
      BTAddress addr;
      bool found = false;
      unsigned long sdl = millis() + 15000;
      while (!found && millis() < sdl) {
        BLEScan::Results res = scn.startBlocking(3000);
        for (const auto &dev : res) {
          BLEAdvertisedDevice d = dev;
          if (d.getName() == targetName) {
            addr = d.getAddress();
            found = true;
            break;
          }
        }
        scn.clearResults();
      }
      uint32_t sent = 0;
      if (found) {
        BLEClient client = BLE.createClient();
        BTStatus cs = client.connect(addr);
        if (cs) {
          BTStatus ib = BLEAudioIso::begin();
          Serial.printf("[CLIENT] IsoCis transport: %s\n", ib.toString());
          BLEAudioIso::CisParams params;
          BLEAudioIso::Channel *cis = BLEAudioIso::connectCis(client.getHandle(), params);
          if (cis) {
            cis->onConnected([](BLEAudioIso::Channel &) {
              syncPhaseFromHost();
              isoCisClientConnected = true;
            });
            unsigned long cdl = millis() + 15000;
            while (!isoCisClientConnected && millis() < cdl) {
              delay(50);
            }
            uint8_t sdu[120];
            uint16_t seq = 0;
            for (int i = 0; i < 200 && isoCisClientConnected; i++) {
              memset(sdu, (uint8_t)seq, sizeof(sdu));
              if (cis->send(sdu, sizeof(sdu), seq)) {
                sent++;
              }
              seq++;
              delay(10);
            }
            delay(1000);
          } else {
            Serial.println("[CLIENT] IsoCis connect FAILED");
          }
          BLEAudioIso::end();
          client.disconnect();
        } else {
          Serial.println("[CLIENT] IsoCis ACL connect FAILED");
        }
      } else {
        Serial.println("[CLIENT] IsoCis target not found");
      }
      Serial.printf("[CLIENT] IsoCis result connected=%d sent=%lu\n", (int)isoCisClientConnected, (unsigned long)sent);
    }
    BLE.end(false);
    delay(500);
    (void)BLE.begin("BLE_CLT");
  }
#else
  Serial.println("[CLIENT] IsoCis not supported");
#endif
  Serial.println("[CLIENT] Phase2 iso_cis done");

  // ===== Phase 3: ISO BIS transport integrity (client = BIS Receiver) =====
  // Arms a BIG sync, then syncs to the server's periodic train. The scan handler
  // forwards the BIGInfo report into the ISO engine, which issues the BIG sync;
  // received SDUs are counted. Needs host ISO + BLE5; self-skips otherwise.
  // Restores a normal stack afterward.
  waitForPhase(3);
#if BLE_ISO_SUPPORTED && BLE5_SUPPORTED
  {
    BLE.end(false);
    delay(500);
    isoBisClientSynced = false;
    isoBisRxCount = 0;
    BTStatus st = BLE.begin("BLE_CLT_ISO2");
    if (st) {
      BTStatus ib = BLEAudioIso::begin();
      Serial.printf("[CLIENT] IsoBis transport: %s\n", ib.toString());
      BLEAudioIso::BigParams params;
      BLEAudioIso::Channel *bis = BLEAudioIso::syncBig(1, params);
      if (bis) {
        bis->onConnected([](BLEAudioIso::Channel &) {
          syncPhaseFromHost();
          isoBisClientSynced = true;
        });
        bis->onReceive([](BLEAudioIso::Channel &, const BLEAudioIso::SduInfo &info, const uint8_t *, uint16_t) {
          if (info.valid) {
            isoBisRxCount = isoBisRxCount + 1;
          }
        });
        bool found = false;
        BLEScan scan = BLE.getScan();
        scan.onResult([&](BLEAdvertisedDevice dev) {
          syncPhaseFromHost();
          if (!found && dev.getName() == targetName) {
            found = true;
            targetAddr = dev.getAddress();
            scan.createPeriodicSync(targetAddr, 1);
          }
        });
        scan.startExtended(30000);
        unsigned long deadline = millis() + 28000;
        while (millis() < deadline) {
          delay(100);
        }
        scan.stop();
      } else {
        Serial.println("[CLIENT] IsoBis sync arm FAILED");
      }
      BLEAudioIso::end();
      Serial.printf("[CLIENT] IsoBis result synced=%d received=%lu\n", (int)isoBisClientSynced, (unsigned long)isoBisRxCount);
    }
    BLE.end(false);
    delay(500);
    (void)BLE.begin("BLE_CLT");
  }
#else
  Serial.println("[CLIENT] IsoBis not supported");
#endif
  Serial.println("[CLIENT] Phase3 iso_bis done");

  // ===== Phase 4: BAP unicast transport (client = BAP Unicast Client) =====
  // Establishes the ACL to the unicast server, runs the full BAP setup
  // (discover -> config -> QoS -> enable -> CIS connect -> start) via the audio
  // Unicast Client, then streams transparent SDUs to the server's sink ASE and
  // reports the count sent. Needs the LE Audio engine; self-skips otherwise.
  // Restores a normal stack afterward.
  waitForPhase(4);
#if BLE_AUDIO_SUPPORTED
  {
    BLE.end(false);
    delay(500);
    bapTxStreaming = false;
    uint32_t sent = 0;
    BTStatus st = BLE.begin("BLE_CLT_BAP");
    if (st) {
      BLEAudio audio = BLE.getAudioController();
      if (audio.begin()) {
        BLEAudioUnicastClient uc = audio.createUnicastClient();
        uc.setPreset(BLEAudioCodecPreset::LC3_16_2_1);
        BLEAudioStream tx = uc.txStream();
        tx.onStarted([](BLEAudioStream &) {
          bapTxStreaming = true;
        });
        if (audio.start()) {
          BLEScan scn = BLE.getScan();
          scn.resetCallbacks();
          scn.clearResults();
          scn.setActiveScan(true);
          BTAddress addr;
          bool found = false;
          unsigned long sdl = millis() + 15000;
          while (!found && millis() < sdl) {
            BLEScan::Results res = scn.startBlocking(3000);
            for (const auto &dev : res) {
              BLEAdvertisedDevice d = dev;
              if (d.getName() == targetName) {
                addr = d.getAddress();
                found = true;
                break;
              }
            }
            scn.clearResults();
          }
          if (found) {
            BLEClient client = BLE.createClient();
            BTStatus cs = client.connect(addr);
            if (cs) {
              // Let MTU exchange + engine GATT discovery settle before BAP ASE discovery.
              delay(2500);
              BTStatus bs = uc.connect(client.getHandle());
              Serial.printf("[CLIENT] BapUnicast setup: %s\n", bs.toString());
              unsigned long cdl = millis() + 15000;
              while (!bapTxStreaming && millis() < cdl) {
                delay(50);
              }
              if (bapTxStreaming) {
                uint8_t sdu[120];
                uint16_t seq = 0;
                for (int i = 0; i < 300 && bapTxStreaming; i++) {
                  memset(sdu, (uint8_t)seq, sizeof(sdu));
                  if (tx.write(sdu, sizeof(sdu), seq)) {
                    sent++;
                  }
                  seq++;
                  delay(10);
                }
                delay(1000);
              } else {
                Serial.println("[CLIENT] BapUnicast stream did not start");
              }
              uc.reset();
              client.disconnect();
            } else {
              Serial.println("[CLIENT] BapUnicast ACL connect FAILED");
            }
          } else {
            Serial.println("[CLIENT] BapUnicast target not found");
          }
        }
        audio.end();
      }
      Serial.printf("[CLIENT] BapUnicast result streaming=%d sent=%lu\n", (int)bapTxStreaming, (unsigned long)sent);
    }
    BLE.end(false);
    delay(500);
    (void)BLE.begin("BLE_CLT");
  }
#else
  Serial.println("[CLIENT] BapUnicast not supported");
#endif
  Serial.println("[CLIENT] Phase4 bap_unicast done");

  // ===== Phase 5: BAP broadcast transport (client = BAP Broadcast Sink) =====
  // Scans for the server's Auracast broadcast, PA-syncs, syncs the BIG, and
  // counts the transparent SDUs streamed Source->Sink over the BIS. Needs the
  // LE Audio engine; self-skips otherwise. Restores a normal stack afterward.
  waitForPhase(5);
#if BLE_AUDIO_SUPPORTED
  {
    BLE.end(false);
    delay(500);
    bapBcastRxCount = 0;
    BTStatus st = BLE.begin("BLE_CLT_BCAST");
    if (st) {
      BLEAudio audio = BLE.getAudioController();
      if (audio.begin()) {
        BLEAudioBroadcastSink sink = audio.createBroadcastSink();
        sink.setPreset(BLEAudioCodecPreset::LC3_16_2_1).setTargetName(targetName);
        BLEAudioStream rx = sink.sinkStream();
        rx.onReceive([](BLEAudioStream &, const BLEAudioSduInfo &info, const uint8_t *, uint16_t) {
          if (info.packetStatus == 0) {
            bapBcastRxCount = bapBcastRxCount + 1;
          }
        });
        if (audio.start() && sink.start()) {
          Serial.println("[CLIENT] BapBroadcast scanning");
          unsigned long deadline = millis() + 45000;
          while (millis() < deadline) {
            delay(100);
          }
          sink.stop();
        }
        audio.end();
      }
      Serial.printf("[CLIENT] BapBroadcast result received=%lu\n", (unsigned long)bapBcastRxCount);
    }
    BLE.end(false);
    delay(500);
    (void)BLE.begin("BLE_CLT");
  }
#else
  Serial.println("[CLIENT] BapBroadcast not supported");
#endif
  Serial.println("[CLIENT] Phase5 bap_broadcast done");

  // ===== Phase 6: LC3 loopback (client = Unicast Client source + Recorder) =====
  // Connects to the Unicast Server, then hands its source ASE to a
  // BLEAudioRecorder fed by a synthetic 1 kHz tone; the recorder LC3-encodes the
  // PCM and streams the frames over the CIS. The server decodes them and asserts
  // the audio arrived (see the server's samples/meanamp result). Needs the LC3
  // codec (BLE_AUDIO_LC3_SUPPORTED); self-skips otherwise.
  waitForPhase(6);
#if BLE_AUDIO_LC3_SUPPORTED
  {
    BLE.end(false);
    delay(500);
    bapTxStreaming = false;
    BTStatus st = BLE.begin("BLE_CLT_LC3");
    if (st) {
      BLEAudio audio = BLE.getAudioController();
      if (audio.begin()) {
        BLEAudioUnicastClient uc = audio.createUnicastClient();
        uc.setPreset(BLEAudioCodecPreset::LC3_16_2_1);
        BLEAudioStream tx = uc.txStream();
        tx.onStarted([](BLEAudioStream &) {
          bapTxStreaming = true;
        });
        tx.onStopped([](BLEAudioStream &, uint8_t) {
          bapTxStreaming = false;
        });
        if (audio.start()) {
          // A deterministic 1 kHz tone (16 samples/cycle at 16 kHz) so the
          // server sees a non-silent, well-defined signal after decode.
          BLEAudioRecorder recorder(
            tx,
            [](int16_t *pcm, size_t maxSamples) -> size_t {
              static uint32_t ph = 0;
              for (size_t i = 0; i < maxSamples; i++) {
                float s = sinf((float)ph * 2.0f * 3.14159265f / 16.0f);
                pcm[i] = (int16_t)(s * 8000.0f);
                ph++;
              }
              return maxSamples;
            },
            BLEAudioCodecPreset::LC3_16_2_1
          );
          bool recStarted = (bool)recorder.begin();
          BLEScan scn = BLE.getScan();
          scn.resetCallbacks();
          scn.clearResults();
          scn.setActiveScan(true);
          BTAddress addr;
          bool found = false;
          unsigned long sdl = millis() + 15000;
          while (recStarted && !found && millis() < sdl) {
            BLEScan::Results res = scn.startBlocking(3000);
            for (const auto &dev : res) {
              BLEAdvertisedDevice d = dev;
              if (d.getName() == targetName) {
                addr = d.getAddress();
                found = true;
                break;
              }
            }
            scn.clearResults();
          }
          if (found) {
            BLEClient client = BLE.createClient();
            BTStatus cs = client.connect(addr);
            if (cs) {
              delay(2500);  // MTU exchange + engine GATT discovery
              BTStatus bs = uc.connect(client.getHandle());
              Serial.printf("[CLIENT] Lc3Loopback setup: %s\n", bs.toString());
              unsigned long cdl = millis() + 15000;
              while (!bapTxStreaming && millis() < cdl) {
                delay(50);
              }
              if (bapTxStreaming) {
                // The recorder task encodes + sends autonomously; just let it run.
                delay(4000);
              } else {
                Serial.println("[CLIENT] Lc3Loopback stream did not start");
              }
              recorder.end();
              uc.reset();
              client.disconnect();
            } else {
              Serial.println("[CLIENT] Lc3Loopback ACL connect FAILED");
            }
          } else {
            Serial.println("[CLIENT] Lc3Loopback target not found");
          }
          recorder.end();
        }
        audio.end();
      }
      Serial.printf("[CLIENT] Lc3Loopback result streaming=%d\n", (int)bapTxStreaming);
    }
    BLE.end(false);
    delay(500);
    (void)BLE.begin("BLE_CLT");
  }
#else
  Serial.println("[CLIENT] Lc3Loopback not supported");
#endif
  Serial.println("[CLIENT] Phase6 lc3_loopback done");

  // ===== Phase 7: Control profiles (client = the "phone" controller) =====
  // Connects once to the server acceptor, then drives every control profile
  // over that single ACL: CAP initiator (CAS discovery), VCP controller
  // (discover + setVolume), MICP controller (discover + mute), MCP controller
  // (discover + play), and CCP controller (discover + originate). Each sub-step
  // is independently timed and its outcome recorded as a bit. Needs the LE Audio
  // engine; self-skips otherwise. Restores a normal stack afterward.
  waitForPhase(7);
#if BLE_AUDIO_SUPPORTED
  {
    BLE.end(false);
    delay(500);
    int capDisc = 0, vcpOk = 0, micOk = 0, mediaOk = 0, callOk = 0;
    BTStatus st = BLE.begin("BLE_CLT_CTRL");
    if (st) {
      BLEAudio audio = BLE.getAudioController();
      if (audio.begin()) {
        BLEAudioCapInitiator capInit = audio.createCapInitiator();
        volatile bool capDone = false;
        capInit.onDiscovered([&capDone, &capDisc](BTStatus s2, bool hasCsis) {
          (void)hasCsis;
          capDisc = s2 ? 1 : 0;
          capDone = true;
        });

        BLEAudioVolumeController vc = audio.createVolumeController();
        volatile bool vcDisc = false;
        vc.onDiscovered([&vcDisc](BTStatus s2, uint8_t, uint8_t) {
          vcDisc = (bool)s2;
        });

        BLEAudioMicController mc = audio.createMicController();
        volatile bool micDisc = false;
        mc.onDiscovered([&micDisc](BTStatus s2, uint8_t) {
          micDisc = (bool)s2;
        });

        BLEAudioMediaController mediaCtl = audio.createMediaController();
        volatile bool mediaDisc = false;
        mediaCtl.onDiscovered([&mediaDisc](BTStatus s2) {
          mediaDisc = (bool)s2;
        });

        BLEAudioCallController callCtl = audio.createCallController();
        volatile bool callDisc = false;
        callCtl.onDiscovered([&callDisc](BTStatus s2, bool gtbs) {
          (void)gtbs;
          callDisc = (bool)s2;
        });

        if (audio.start()) {
          BLEScan scn = BLE.getScan();
          scn.resetCallbacks();
          scn.clearResults();
          scn.setActiveScan(true);
          BTAddress addr;
          bool found = false;
          unsigned long sdl = millis() + 15000;
          while (!found && millis() < sdl) {
            BLEScan::Results res = scn.startBlocking(3000);
            for (const auto &dev : res) {
              BLEAdvertisedDevice d = dev;
              if (d.getName() == targetName) {
                addr = d.getAddress();
                found = true;
                break;
              }
            }
            scn.clearResults();
          }
          if (found) {
            BLEClient client = BLE.createClient();
            BTStatus cs = client.connect(addr);
            if (cs) {
              delay(2500);  // MTU exchange + engine GATT discovery
              uint16_t h = client.getHandle();

              // CAP: confirm the peer is a Common Audio Profile acceptor.
              capInit.discover(h);
              for (int i = 0; i < 60 && !capDone; i++) {
                delay(50);
              }

              // VCP: discover then set an absolute volume the server observes.
              vc.discover(h);
              for (int i = 0; i < 60 && !vcDisc; i++) {
                delay(50);
              }
              if (vcDisc && vc.setVolume(200)) {
                vcpOk = 1;
              }
              delay(300);

              // MICP: discover then mute the remote microphone.
              mc.discover(h);
              for (int i = 0; i < 60 && !micDisc; i++) {
                delay(50);
              }
              if (micDisc && mc.mute()) {
                micOk = 1;
              }
              delay(300);

              // MCP: discover then send Play to the reference media player.
              mediaCtl.discover(h);
              for (int i = 0; i < 60 && !mediaDisc; i++) {
                delay(50);
              }
              if (mediaDisc && mediaCtl.play(h)) {
                mediaOk = 1;
              }
              delay(300);

              // CCP: discover then originate a call on the peer's GTBS.
              callCtl.discover(h);
              for (int i = 0; i < 60 && !callDisc; i++) {
                delay(50);
              }
              if (callDisc && callCtl.originate(h, "tel:123456")) {
                callOk = 1;
              }
              delay(500);

              client.disconnect();
            } else {
              Serial.println("[CLIENT] ControlProfiles ACL connect FAILED");
            }
          } else {
            Serial.println("[CLIENT] ControlProfiles target not found");
          }
        }
        audio.end();
      }
      Serial.printf("[CLIENT] ControlProfiles result cap=%d vcp=%d mic=%d media=%d call=%d\n", capDisc, vcpOk, micOk, mediaOk, callOk);
    }
    BLE.end(false);
    delay(500);
    (void)BLE.begin("BLE_CLT");
  }
#else
  Serial.println("[CLIENT] ControlProfiles not supported");
#endif
  Serial.println("[CLIENT] Phase7 control_profiles done");

  // ===== Phase 8: Top-level profiles (client = TMAP discoverer; GMAP expected fail) =====
  // Connects to the server, discovers TMAS, then attempts GMAS. On release/v6.1
  // GMAS is not on the air, so gmapRoles stays -1 while tmapRoles must be CT|UMR.
  // Needs TMAP/GMAP compiled in; self-skips otherwise. Restores a normal stack after.
  waitForPhase(8);
#if BLE_AUDIO_TMAP_SUPPORTED && BLE_AUDIO_GMAP_SUPPORTED
  {
    BLE.end(false);
    delay(500);
    int tmapRoles = -1, gmapRoles = -1;
    BTStatus st = BLE.begin("BLE_CLT_TOP");
    if (st) {
      BLEAudio audio = BLE.getAudioController();
      if (audio.begin()) {
        BLEAudioTmap tmap = audio.createTmap();  // no local roles: client only
        volatile bool tmapDone = false;
        tmap.onDiscovered([&tmapDone, &tmapRoles](BTStatus s2, BLEAudioTmapRole roles) {
          tmapRoles = s2 ? (int)(uint8_t)roles : -1;
          tmapDone = true;
        });
        BLEAudioGmap gmap = audio.createGmap();
        volatile bool gmapDone = false;
        gmap.onDiscovered([&gmapDone, &gmapRoles](BTStatus s2, BLEAudioGmapRole roles) {
          gmapRoles = s2 ? (int)(uint8_t)roles : -1;
          gmapDone = true;
        });

        if (audio.start()) {
          BLEScan scn = BLE.getScan();
          scn.resetCallbacks();
          scn.clearResults();
          scn.setActiveScan(true);
          BTAddress addr;
          bool found = false;
          unsigned long sdl = millis() + 15000;
          while (!found && millis() < sdl) {
            BLEScan::Results res = scn.startBlocking(3000);
            for (const auto &dev : res) {
              BLEAdvertisedDevice d = dev;
              if (d.getName() == targetName) {
                addr = d.getAddress();
                found = true;
                break;
              }
            }
            scn.clearResults();
          }
          if (found) {
            BLEClient client = BLE.createClient();
            BTStatus cs = client.connect(addr);
            if (cs) {
              delay(2500);  // MTU exchange + engine GATT discovery
              uint16_t h = client.getHandle();
              tmap.discover(h);
              for (int i = 0; i < 60 && !tmapDone; i++) {
                delay(50);
              }
              gmap.discover(h);
              for (int i = 0; i < 60 && !gmapDone; i++) {
                delay(50);
              }
              client.disconnect();
            } else {
              Serial.println("[CLIENT] TopProfiles ACL connect FAILED");
            }
          } else {
            Serial.println("[CLIENT] TopProfiles target not found");
          }
        }
        audio.end();
      }
      Serial.printf("[CLIENT] TopProfiles result tmap=%d gmap=%d\n", tmapRoles, gmapRoles);
    }
    BLE.end(false);
    delay(500);
    (void)BLE.begin("BLE_CLT");
  }
#else
  Serial.println("[CLIENT] TopProfiles not supported");
#endif
  Serial.println("[CLIENT] Phase8 top_profiles done");

  // ===== Phase 9: Hearing Access Service (client = controller) =====
  // Connects to the hearing-aid server, discovers its HAS, reads the preset
  // list, and switches the active preset to index 2. Records how many preset
  // records it read and whether the switch was accepted. Needs the HAS client
  // (BLE_AUDIO_HAS_CLIENT_SUPPORTED); self-skips otherwise. Restores a normal
  // stack afterward.
  waitForPhase(9);
#if BLE_AUDIO_HAS_CLIENT_SUPPORTED
  {
    BLE.end(false);
    delay(500);
    int readCount = 0, switched = 0, discOk = 0;
    BTStatus st = BLE.begin("BLE_CLT_HA");
    if (st) {
      BLEAudio audio = BLE.getAudioController();
      if (audio.begin()) {
        BLEAudioHearingAidController hac = audio.createHearingAidController();
        volatile bool discDone = false;
        hac.onDiscovered([&discDone, &discOk](BTStatus s2, BLEHearingAidType type) {
          (void)type;
          discOk = s2 ? 1 : 0;
          discDone = true;
        });
        hac.onPreset([&readCount](uint8_t index, bool available, const std::string &name, bool isLast) {
          (void)index;
          (void)available;
          (void)name;
          (void)isLast;
          readCount = readCount + 1;
        });
        volatile bool switchDone = false;
        hac.onPresetSwitch([&switchDone, &switched](BTStatus s2, uint8_t index) {
          (void)index;
          switched = s2 ? 1 : 0;
          switchDone = true;
        });

        if (audio.start()) {
          BLEScan scn = BLE.getScan();
          scn.resetCallbacks();
          scn.clearResults();
          scn.setActiveScan(true);
          BTAddress addr;
          bool found = false;
          unsigned long sdl = millis() + 15000;
          while (!found && millis() < sdl) {
            BLEScan::Results res = scn.startBlocking(3000);
            for (const auto &dev : res) {
              BLEAdvertisedDevice d = dev;
              if (d.getName() == targetName) {
                addr = d.getAddress();
                found = true;
                break;
              }
            }
            scn.clearResults();
          }
          if (found) {
            BLEClient client = BLE.createClient();
            BTStatus cs = client.connect(addr);
            if (cs) {
              delay(2500);  // MTU exchange + engine GATT discovery
              uint16_t h = client.getHandle();
              hac.discover(h);
              for (int i = 0; i < 60 && !discDone; i++) {
                delay(50);
              }
              if (discOk) {
                hac.readPresets(1, 255);
                delay(1500);  // let all preset records stream in
                if (hac.setActivePreset(2)) {
                  for (int i = 0; i < 60 && !switchDone; i++) {
                    delay(50);
                  }
                }
              }
              delay(300);
              client.disconnect();
            } else {
              Serial.println("[CLIENT] HearingAid ACL connect FAILED");
            }
          } else {
            Serial.println("[CLIENT] HearingAid target not found");
          }
        }
        audio.end();
      }
      Serial.printf("[CLIENT] HearingAid result disc=%d read=%d switched=%d\n", discOk, readCount, switched);
    }
    BLE.end(false);
    delay(500);
    (void)BLE.begin("BLE_CLT");
  }
#else
  Serial.println("[CLIENT] HearingAid not supported");
#endif
  Serial.println("[CLIENT] Phase9 hearing_aid done");

  // ===== Phase 10: Memory release + reinit guard =====
  waitForPhase(10);

  size_t heapBeforeRelease = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[CLIENT] Heap before release: %u\n", (unsigned)heapBeforeRelease);

  BLE.end(true);

  delay(500);
  size_t heapAfterRelease = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[CLIENT] Heap after release: %u\n", (unsigned)heapAfterRelease);

  int32_t freed = (int32_t)heapAfterRelease - (int32_t)heapBeforeRelease;
  Serial.printf("[CLIENT] Memory freed: %ld bytes\n", (long)freed);
  if (freed >= 10240) {
    Serial.println("[CLIENT] Memory release OK");
  } else {
    Serial.println("[CLIENT] Memory release INSUFFICIENT");
  }

  status = BLE.begin("ShouldFail");
  if (!status) {
    Serial.println("[CLIENT] Reinit blocked OK");
  } else {
    Serial.println("[CLIENT] Reinit was NOT blocked");
  }

  Serial.println("[CLIENT] All phases complete");
}

void loop() {
  delay(1000);
}
