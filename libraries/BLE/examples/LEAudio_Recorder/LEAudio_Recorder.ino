/*
 * LE Audio -- Turnkey Recorder (I2S in -> LC3 encode -> Unicast Client source)
 *
 * Brings up a BAP Unicast Client, connects to a Unicast Server, and hands its
 * source ASE to a BLEAudioRecorder, which captures PCM from an I2S microphone /
 * ADC, LC3-encodes it, and streams the frames over the CIS. Together with the
 * LEAudio_Player example this forms a full one-way on-air voice link between two
 * LE-Audio-capable boards.
 *
 * Requires the LC3 codec (managed component esp_audio_codec) to be compiled
 * into the core; on builds without it the sketch self-reports and idles.
 *
 * Wire an I2S microphone (e.g. INMP441, ICS-43434) to the pins below.
 *
 * Callback style: named functions.
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <Arduino.h>
#include <BLE.h>

#if BLE_AUDIO_LC3_SUPPORTED

// ---- I2S input pinout (edit for your board / mic) ----
#define I2S_BCLK 5   // bit clock  (BCLK / SCK)
#define I2S_WS   6   // word select (LRCLK / WS)
#define I2S_DIN  4   // data in    (SD / DOUT on the mic)
#define I2S_PORT 0

static const char *TARGET_NAME = "BAP Unicast Server";
static const BLEAudioCodecPreset PRESET = BLEAudioCodecPreset::LC3_16_2_1;

BLEAudio audio;
BLEAudioUnicastClient audioClient;
BLEAudioRecorder recorder;
BLEClient client;

BTAddress serverAddress;
volatile bool doConnect = false;

void onTxStarted(BLEAudioStream &) {
  Serial.println("[tx] streaming started -- capturing mic -> LC3 -> CIS");
}

void onTxStopped(BLEAudioStream &, uint8_t reason) {
  Serial.printf("[tx] streaming stopped (reason 0x%02X)\n", reason);
}

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

  // Let the engine finish MTU exchange + GATT discovery before ASE discovery.
  delay(2500);

  st = audioClient.connect(client.getHandle());
  if (!st) {
    Serial.printf("BAP setup failed: %s\n", st.toString());
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== LE Audio Recorder (I2S -> LC3) ===");

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
  audioClient.setPreset(PRESET);

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

  // Turnkey capture: the recorder owns the LC3 encoder, capture task, and I2S
  // channel; it encodes mic PCM to the source stream, sending once the CIS is up.
  BLEAudioI2sConfig i2s;
  i2s.bclk = I2S_BCLK;
  i2s.ws = I2S_WS;
  i2s.din = I2S_DIN;
  i2s.port = I2S_PORT;

  recorder = BLEAudioRecorder(tx, i2s, PRESET);
  st = recorder.begin();
  if (!st) {
    Serial.printf("recorder.begin failed: %s\n", st.toString());
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
  delay(100);
}

#else  // !BLE_AUDIO_LC3_SUPPORTED

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("LEAudio_Recorder requires the LC3 codec (esp_audio_codec) in this build.");
}

void loop() {
  delay(1000);
}

#endif /* BLE_AUDIO_LC3_SUPPORTED */
