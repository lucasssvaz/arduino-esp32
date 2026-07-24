/*
 * LE Audio -- BAP Broadcast Source (Auracast transmitter)
 *
 * Brings up the LE Audio engine, creates a single-stream (mono) Broadcast
 * Source, announces it via extended + periodic advertising (Broadcast Audio
 * Announcement + BASE), and streams transparent SDUs over a Broadcast
 * Isochronous Group (BIG). No connection is involved -- any Broadcast Sink in
 * range can sync and receive.
 *
 * This is a raw-SDU demo: no LC3 encode / I2S yet (that is the turnkey
 * BLEAudioRecorder, added later). Pair it with the LEAudio_BroadcastSink example
 * on a second LE-Audio-capable board (e.g. ESP32-S31).
 *
 * Callback style: named functions.
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <Arduino.h>
#include <BLE.h>

static const char *BROADCAST_NAME = "BAP Broadcast Source";
static const uint32_t BROADCAST_ID = 0x123456;

BLEAudio audio;
BLEAudioBroadcastSource source;

volatile bool streaming = false;

void onSourceStarted(BLEAudioStream &) {
  Serial.println("[source] streaming started -- sending SDUs");
  streaming = true;
}

void onSourceStopped(BLEAudioStream &, uint8_t reason) {
  Serial.printf("[source] streaming stopped (reason 0x%02X)\n", reason);
  streaming = false;
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== LE Audio Broadcast Source ===");

  BTStatus st = BLE.begin(BROADCAST_NAME);
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

  source = audio.createBroadcastSource();
  source.setPreset(BLEAudioCodecPreset::LC3_16_2_1).setBroadcastId(BROADCAST_ID).setName(BROADCAST_NAME);

  BLEAudioStream src = source.sourceStream();
  src.onStarted(onSourceStarted);
  src.onStopped(onSourceStopped);

  // Creates the source + BASE (single coordinated commit).
  st = audio.start();
  if (!st) {
    Serial.printf("audio.start failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  // Brings up the ext + periodic advertising carrier and starts the BIG.
  st = source.start();
  if (!st) {
    Serial.printf("source.start failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  Serial.printf("Broadcasting \"%s\" (id 0x%06X)...\n", BROADCAST_NAME, (unsigned)BROADCAST_ID);
}

void loop() {
  if (streaming) {
    static uint8_t sdu[120];
    static uint16_t seq = 0;
    memset(sdu, (uint8_t)seq, sizeof(sdu));
    BLEAudioStream src = source.sourceStream();
    if (src.write(sdu, sizeof(sdu), seq)) {
      seq++;
    }
    delay(10);  // ~10 ms SDU interval (matches LC3_16_2_1)
  } else {
    delay(100);
  }
}
