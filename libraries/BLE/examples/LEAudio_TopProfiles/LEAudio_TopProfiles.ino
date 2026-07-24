/*
 * LE Audio -- Top-Level Profiles (TMAP + GMAP identity)
 *
 * A generic device that advertises its top-level LE Audio profile identity:
 *
 *   - TMAP (Telephony & Media Audio Profile): Call Terminal + Unicast Media
 *     Receiver -- i.e. a headset/earbud that takes calls and receives media.
 *   - GMAP (Gaming Audio Profile): Unicast Game Terminal -- the headset end of
 *     a low-latency game-audio link.
 *
 * These profiles do NOT move audio themselves; they publish a small TMAS/GMAS
 * identity service so a central (phone/console) can discover which top-level
 * roles this device plays. The actual audio flows through the CAP acceptor +
 * unicast server + VCP renderer that also come up here and that back those
 * roles' prerequisites. A TMAP/GMAP central connects, discovers the identity,
 * and then streams via CAP/BAP as shown in the other LEAudio_* examples.
 *
 * Requires the LE Audio engine with TMAP + GMAP compiled in
 * (BLE_AUDIO_TMAP_SUPPORTED + BLE_AUDIO_GMAP_SUPPORTED); on builds without them
 * the sketch self-reports and idles.
 *
 * Callback style: named functions.
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <Arduino.h>
#include <BLE.h>

#if BLE_AUDIO_TMAP_SUPPORTED && BLE_AUDIO_GMAP_SUPPORTED

static const char *DEVICE_NAME = "LE Audio Terminal";

BLEAudio audio;
BLEAudioCapAcceptor capAcceptor;
BLEAudioUnicastServer unicastServer;
BLEAudioVolumeRenderer volumeRenderer;
BLEAudioTmap tmap;
BLEAudioGmap gmap;

void onVolumeChanged(uint8_t volume, bool muted) {
  Serial.printf("[VCP] local volume=%u muted=%d\n", volume, muted);
}

void haltWith(const char *what, BTStatus st) {
  Serial.printf("%s failed: %s\n", what, st.toString());
  while (true) {
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== LE Audio Top-Level Profiles (TMAP + GMAP) ===");

  BTStatus st = BLE.begin(DEVICE_NAME);
  if (!st) {
    haltWith("BLE.begin", st);
  }

  audio = BLE.getAudioController();
  st = audio.begin();
  if (!st) {
    haltWith("audio.begin", st);
  }

  // Underlying roles that satisfy the advertised TMAP/GMAP prerequisites:
  // a CAP acceptor (CAS), a unicast server (PACS/ASCS sink+source), and a
  // VCP renderer (VCS) for volume control.
  capAcceptor = audio.createCapAcceptor();
  capAcceptor.setSetSize(1).setRank(1);

  unicastServer = audio.createUnicastServer();
  unicastServer.enableSink(true)
    .enableSource(true)
    .setSinkContexts(BLEAudioContext::Media | BLEAudioContext::Conversational);

  volumeRenderer = audio.createVolumeRenderer();
  volumeRenderer.setInitialVolume(128).onStateChanged(onVolumeChanged);

  // Top-level identity: this device is a TMAP Call Terminal + Unicast Media
  // Receiver and a GMAP Unicast Game Terminal (with sink support).
  tmap = audio.createTmap();
  tmap.setRoles(BLEAudioTmapRole::CallTerminal | BLEAudioTmapRole::UnicastMediaReceiver);

  gmap = audio.createGmap();
  gmap.setRoles(BLEAudioGmapRole::UnicastGameTerminal)
    .setFeatures(0, 0x04 /* UGT: Sink support */, 0, 0);

  // Single coordinated commit publishes CAS + PACS/ASCS + VCS + TMAS + GMAS.
  st = audio.start();
  if (!st) {
    haltWith("audio.start", st);
  }

  BLEAdvertising adv = BLE.getAdvertising();
  adv.setName(DEVICE_NAME);
  st = adv.start();
  if (!st) {
    haltWith("advertising", st);
  }

  Serial.println("Ready. Advertising TMAP (CT+UMR) + GMAP (UGT) identity.");
  Serial.println("Connect a TMAP/GMAP central to discover the roles and stream.");
}

void loop() {
  delay(1000);
}

#else  // !(BLE_AUDIO_TMAP_SUPPORTED && BLE_AUDIO_GMAP_SUPPORTED)

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("LEAudio_TopProfiles requires TMAP + GMAP (CONFIG_BT_TMAP=y, CONFIG_BT_GMAP=y) in this build.");
}

void loop() {
  delay(1000);
}

#endif /* BLE_AUDIO_TMAP_SUPPORTED && BLE_AUDIO_GMAP_SUPPORTED */
