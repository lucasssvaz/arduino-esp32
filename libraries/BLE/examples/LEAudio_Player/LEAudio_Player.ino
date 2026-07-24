/*
 * LE Audio -- Turnkey Player (Unicast Server sink -> LC3 decode -> I2S out)
 *
 * Brings up a BAP Unicast Server (PACS + ASCS), then hands its sink ASE to a
 * BLEAudioPlayer, which decodes the incoming LC3 SDUs and streams the PCM to an
 * I2S DAC -- the first on-air audio path. Pair it with the LEAudio_Recorder
 * example (or any Unicast Client) on a second LE-Audio-capable board.
 *
 * Requires the LC3 codec (managed component esp_audio_codec) to be compiled
 * into the core; on builds without it the sketch self-reports and idles.
 *
 * Wire an I2S DAC (e.g. PCM5102, MAX98357A, or a codec's DAC) to the pins below.
 *
 * Callback style: lambdas.
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <Arduino.h>
#include <BLE.h>

#if BLE_AUDIO_LC3_SUPPORTED

// ---- I2S output pinout (edit for your board / DAC) ----
#define I2S_BCLK 5   // bit clock  (BCLK / SCK)
#define I2S_WS   6   // word select (LRCLK / WS)
#define I2S_DOUT 7   // data out   (DIN on the DAC)
#define I2S_PORT 0

static const char *DEVICE_NAME = "BAP Unicast Server";
static const BLEAudioCodecPreset PRESET = BLEAudioCodecPreset::LC3_16_2_1;

BLEAudio audio;
BLEAudioUnicastServer unicastServer;
BLEAudioPlayer player;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== LE Audio Player (LC3 -> I2S) ===");

  BTStatus st = BLE.begin(DEVICE_NAME);
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

  // A sink-only server: it receives audio from the client and plays it.
  unicastServer = audio.createUnicastServer();
  unicastServer.enableSink(true)
    .setSinkContexts(BLEAudioContext::Media | BLEAudioContext::Conversational)
    .setSinkLocation(BLEAudioLocation::FrontLeft | BLEAudioLocation::FrontRight);

  BLEAudioStream sink = unicastServer.sinkStream();
  sink.onStarted([](BLEAudioStream &) {
    Serial.println("[sink] streaming started -- decoding to I2S");
  });
  sink.onStopped([](BLEAudioStream &, uint8_t reason) {
    Serial.printf("[sink] streaming stopped (reason 0x%02X)\n", reason);
  });

  st = audio.start();
  if (!st) {
    Serial.printf("audio.start failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  // Turnkey playback: the player owns the LC3 decoder, jitter buffer, decode
  // task, and I2S channel; it decodes the sink stream straight to the DAC.
  BLEAudioI2sConfig i2s;
  i2s.bclk = I2S_BCLK;
  i2s.ws = I2S_WS;
  i2s.dout = I2S_DOUT;
  i2s.port = I2S_PORT;

  player = BLEAudioPlayer(sink, i2s, PRESET);
  st = player.begin();
  if (!st) {
    Serial.printf("player.begin failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  BLEAdvertising adv = BLE.getAdvertising();
  adv.setName(DEVICE_NAME);
  st = adv.start();
  if (!st) {
    Serial.printf("advertising failed: %s\n", st.toString());
    while (true) {
      delay(1000);
    }
  }

  Serial.println("Ready. Waiting for a Unicast Client to stream audio...");
}

void loop() {
  delay(1000);
}

#else  // !BLE_AUDIO_LC3_SUPPORTED

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("LEAudio_Player requires the LC3 codec (esp_audio_codec) in this build.");
}

void loop() {
  delay(1000);
}

#endif /* BLE_AUDIO_LC3_SUPPORTED */
