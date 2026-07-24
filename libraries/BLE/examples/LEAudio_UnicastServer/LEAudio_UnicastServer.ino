/*
 * LE Audio -- BAP Unicast Server (acceptor / peripheral)
 *
 * Brings up the LE Audio engine, publishes a BAP Unicast Server (PACS + ASCS),
 * advertises connectably, and logs the transparent SDUs a Unicast Client streams
 * to its sink ASE over a Connected Isochronous Stream (CIS).
 *
 * This is a raw-SDU demo: no LC3 decode / I2S yet (that is the turnkey
 * BLEAudioPlayer, added later). Pair it with the LEAudio_UnicastClient example
 * on a second LE-Audio-capable board (e.g. ESP32-S31).
 *
 * Callback style: lambdas.
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <Arduino.h>
#include <BLE.h>

static const char *DEVICE_NAME = "BAP Unicast Server";

BLEAudio audio;
BLEAudioUnicastServer unicastServer;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== LE Audio Unicast Server ===");

  BTStatus st = BLE.begin(DEVICE_NAME);
  if (!st) {
    Serial.printf("BLE.begin failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  // Bring up the audio engine (GAP/GATT init + event bridge).
  audio = BLE.getAudioController();
  st = audio.begin();
  if (!st) {
    Serial.printf("audio.begin failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  // Publish a Unicast Server that can both receive (sink) and send (source).
  unicastServer = audio.createUnicastServer();
  unicastServer.enableSink(true)
    .enableSource(true)
    .setSinkContexts(BLEAudioContext::Media | BLEAudioContext::Conversational)
    .setSourceContexts(BLEAudioContext::Media | BLEAudioContext::Conversational)
    .setSinkLocation(BLEAudioLocation::FrontLeft | BLEAudioLocation::FrontRight)
    .setSourceLocation(BLEAudioLocation::FrontLeft | BLEAudioLocation::FrontRight);

  // Observe the sink stream (SDUs arriving from the client).
  BLEAudioStream sink = unicastServer.sinkStream();
  sink.onStarted([](BLEAudioStream &) {
    Serial.println("[sink] streaming started");
  });
  sink.onStopped([](BLEAudioStream &, uint8_t reason) {
    Serial.printf("[sink] streaming stopped (reason 0x%02X)\n", reason);
  });
  sink.onReceive([](BLEAudioStream &, const BLEAudioSduInfo &info, const uint8_t *sdu, uint16_t len) {
    static uint32_t count = 0;
    if ((count++ % 100) == 0) {
      Serial.printf("[sink] SDU #%lu seq=%u len=%u valid=%d\n", (unsigned long)count, info.packetSeqNum, len, info.packetStatus == 0);
    }
  });

  // Commit PACS/ASCS (single coordinated GATT commit).
  st = audio.start();
  if (!st) {
    Serial.printf("audio.start failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  // Advertise connectably so the client can establish the ACL.
  BLEAdvertising adv = BLE.getAdvertising();
  adv.setName(DEVICE_NAME);
  st = adv.start();
  if (!st) {
    Serial.printf("advertising failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  Serial.println("Ready. Waiting for a Unicast Client...");
}

void loop() {
  delay(1000);
}
