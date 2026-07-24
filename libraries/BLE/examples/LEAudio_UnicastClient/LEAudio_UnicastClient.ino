/*
 * LE Audio -- BAP Unicast Client (initiator / central)
 *
 * Scans for a BAP Unicast Server, establishes the ACL, then runs the full BAP
 * stream setup (discover -> config -> QoS -> enable -> CIS connect -> start) and
 * streams transparent SDUs to the server's sink ASE.
 *
 * This is a raw-SDU demo: no LC3 encode / I2S yet (that is the turnkey
 * BLEAudioRecorder, added later). Pair it with the LEAudio_UnicastServer example
 * on a second LE-Audio-capable board (e.g. ESP32-S31).
 *
 * Callback style: named functions.
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <Arduino.h>
#include <BLE.h>

static const char *TARGET_NAME = "BAP Unicast Server";

BLEAudio audio;
BLEAudioUnicastClient audioClient;
BLEClient client;

BTAddress serverAddress;
volatile bool doConnect = false;
volatile bool txStreaming = false;

// Named callback: the transmit stream reached the Streaming state.
void onTxStarted(BLEAudioStream &) {
  Serial.println("[tx] streaming started -- sending SDUs");
  txStreaming = true;
}

void onTxStopped(BLEAudioStream &, uint8_t reason) {
  Serial.printf("[tx] streaming stopped (reason 0x%02X)\n", reason);
  txStreaming = false;
}

// Named scan callback: match the server by name, then connect from loop().
void onDeviceFound(BLEAdvertisedDevice device) {
  if (device.getName() != TARGET_NAME) {
    return;
  }
  Serial.printf("Found %s at %s\n", TARGET_NAME, device.getAddress().toString().c_str());
  serverAddress = device.getAddress();
  doConnect = true;
  BLE.getScan().stop();
}

void connectAndStream() {
  client = BLE.createClient();
  BTStatus st = client.connect(serverAddress);
  if (!st) {
    Serial.printf("ACL connect failed: %s\n", st.toString());
    BLE.getScan().start(0);
    return;
  }
  Serial.printf("ACL connected (handle %u); starting BAP setup...\n", client.getHandle());

  // Let the engine finish MTU exchange + its own GATT service discovery (kicked
  // automatically on MTU) before BAP-level ASE discovery runs.
  delay(2500);

  st = audioClient.connect(client.getHandle());
  if (!st) {
    Serial.printf("BAP setup failed: %s\n", st.toString());
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== LE Audio Unicast Client ===");

  BTStatus st = BLE.begin("BAP Unicast Client");
  if (!st) {
    Serial.printf("BLE.begin failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  audio = BLE.getAudioController();
  st = audio.begin();
  if (!st) {
    Serial.printf("audio.begin failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  audioClient = audio.createUnicastClient();
  audioClient.setPreset(BLEAudioCodecPreset::LC3_16_2_1);

  BLEAudioStream tx = audioClient.txStream();
  tx.onStarted(onTxStarted);
  tx.onStopped(onTxStopped);

  st = audio.start();
  if (!st) {
    Serial.printf("audio.start failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  BLEScan scan = BLE.getScan();
  scan.setActiveScan(true);
  scan.onResult(onDeviceFound);
  scan.start(0);
  Serial.printf("Scanning for \"%s\"...\n", TARGET_NAME);
}

void loop() {
  if (doConnect) {
    doConnect = false;
    connectAndStream();
  }

  if (txStreaming) {
    static uint8_t sdu[120];
    static uint16_t seq = 0;
    memset(sdu, (uint8_t)seq, sizeof(sdu));
    BLEAudioStream tx = audioClient.txStream();
    if (tx.write(sdu, sizeof(sdu), seq)) {
      seq++;
    }
    delay(10);  // ~10 ms SDU interval (matches LC3_16_2_1)
  } else {
    delay(100);
  }
}
