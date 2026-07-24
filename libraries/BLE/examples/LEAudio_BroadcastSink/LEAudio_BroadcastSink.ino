/*
 * LE Audio -- BAP Broadcast Sink (Auracast receiver)
 *
 * Brings up the LE Audio engine, registers PACS + the Scan Delegator (BASS),
 * scans for a Broadcast Source, syncs to its periodic-advertising train, decodes
 * the BASE, syncs the Broadcast Isochronous Group (BIG), and logs the transparent
 * SDUs received -- no connection involved.
 *
 * This is a raw-SDU demo: no LC3 decode / I2S yet (that is the turnkey
 * BLEAudioPlayer, added later). Pair it with the LEAudio_BroadcastSource example
 * on a second LE-Audio-capable board (e.g. ESP32-S31).
 *
 * Callback style: lambdas.
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <Arduino.h>
#include <BLE.h>

static const char *TARGET_NAME = "BAP Broadcast Source";

BLEAudio audio;
BLEAudioBroadcastSink sink;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== LE Audio Broadcast Sink ===");

  BTStatus st = BLE.begin("BAP Broadcast Sink");
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

  sink = audio.createBroadcastSink();
  sink.setPreset(BLEAudioCodecPreset::LC3_16_2_1).setTargetName(TARGET_NAME);

  BLEAudioStream rx = sink.sinkStream();
  rx.onStarted([](BLEAudioStream &) {
    Serial.println("[sink] streaming started");
  });
  rx.onStopped([](BLEAudioStream &, uint8_t reason) {
    Serial.printf("[sink] streaming stopped (reason 0x%02X)\n", reason);
  });
  rx.onReceive([](BLEAudioStream &, const BLEAudioSduInfo &info, const uint8_t *, uint16_t len) {
    static uint32_t count = 0;
    if ((count++ % 100) == 0) {
      Serial.printf("[sink] SDU #%lu seq=%u len=%u valid=%d\n", (unsigned long)count, info.packetSeqNum, len, info.packetStatus == 0);
    }
  });

  // Registers PACS + Scan Delegator (BASS) in the coordinated GATT commit.
  st = audio.start();
  if (!st) {
    Serial.printf("audio.start failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  // Scan and auto-sync to the first matching Broadcast Source.
  st = sink.start();
  if (!st) {
    Serial.printf("sink.start failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  Serial.printf("Scanning for broadcast \"%s\"...\n", TARGET_NAME);
}

void loop() {
  delay(1000);
}
