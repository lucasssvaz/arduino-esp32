// Combined BLE validation test — CLIENT
// Phases: basic lifecycle, BLE5 ext+periodic scan, GATT client, notifications,
//         large writes, descriptors, write-no-response, server disconnect,
//         security, PHY/DLE, reconnect

#include <Arduino.h>
#include <BLE.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"

static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID rwCharUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");
static BLEUUID notifyCharUUID("cba1d466-344c-4be3-ab3f-189f80dd7518");
static BLEUUID indicateCharUUID("d5f782b2-a36e-4d68-947c-0e9a5f2c78e1");
static BLEUUID secureCharUUID("ff1d2614-e2d6-4c87-9154-6625d39ca7f8");
static BLEUUID descCharUUID("a3c87501-8ed3-4bdf-8a39-a01bebede295");
static BLEUUID writeNrCharUUID("1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e");

String targetName;
BTAddress targetAddr;
volatile int currentPhase = 0;

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

// ========================= Phase 1 — Basic Lifecycle =========================

bool phase_basic() {
  BTStatus status = BLE.begin(targetName + "_client");
  if (!status) {
    Serial.printf("[CLIENT] Init FAILED: %s\n", status.toString());
    return false;
  }
  Serial.printf("[CLIENT] Stack: %s\n", BLE.getStackName());
  Serial.println("[CLIENT] Init OK");
  Serial.printf("[CLIENT] Device name: %s\n", BLE.getDeviceName().c_str());
  Serial.printf("[CLIENT] Address: %s\n", BLE.getAddress().toString().c_str());

  BLE.end(false);
  Serial.println("[CLIENT] Deinit OK");
  delay(1000);

  status = BLE.begin(targetName + "_client_reinit");
  if (!status) {
    Serial.printf("[CLIENT] Reinit FAILED: %s\n", status.toString());
    return false;
  }
  Serial.println("[CLIENT] Reinit OK");

  BLE.end(false);
  Serial.println("[CLIENT] Final deinit OK");
  return true;
}

// ============= Phase 2 — BLE5 Extended Scan + Periodic Sync =================

#if BLE5_SUPPORTED
bool phase_ble5_scan() {
  BTStatus status = BLE.begin("BLE_CLT_Ext");
  if (!status) return false;

  volatile bool found = false;
  volatile bool synced = false;
  volatile bool dataReceived = false;

  BLEScan scan = BLE.getScan();

  scan.onResult([&](BLEAdvertisedDevice dev) {
    if (!found && dev.getName() == targetName) {
      Serial.println("[CLIENT] Found target via ext scan!");
      targetAddr = dev.getAddress();
      found = true;
      scan.createPeriodicSync(targetAddr, 1);
    }
  });

  scan.onPeriodicSync([&](uint16_t syncHandle, uint8_t sid, const BTAddress &addr,
                          BLEPhy phy, uint16_t interval) {
    Serial.println("[CLIENT] Synced to periodic adv!");
    synced = true;
  });

  scan.onPeriodicReport([&](uint16_t syncHandle, int8_t rssi, int8_t txPower,
                            const uint8_t *data, size_t len) {
    if (!dataReceived) {
      Serial.println("[CLIENT] Periodic data received");
      dataReceived = true;
      BLE.getScan().stop();
    }
  });

  Serial.println("[CLIENT] BLE5 init OK");
  Serial.println("[CLIENT] Scanning for periodic adv...");
  scan.startExtended(15000);

  if (found) Serial.println("[CLIENT] Extended scan found target");
  if (dataReceived) Serial.println("[CLIENT] Periodic test complete");

  BLE.end(false);
  Serial.println("[CLIENT] BLE5 scan phase done");
  return true;
}
#endif

// ======================== Phase 3+ — GATT Client =============================

bool scanForServer() {
  bool found = false;
  BLEScan scan = BLE.getScan();
  scan.onResult([&](BLEAdvertisedDevice dev) {
    if (dev.getName() == targetName) {
      Serial.println("[CLIENT] Found target server!");
      targetAddr = dev.getAddress();
      found = true;
      BLE.getScan().stop();
    }
  });

  for (int attempt = 1; attempt <= 5 && !found; attempt++) {
    Serial.printf("[CLIENT] Scanning (attempt %d)...\n", attempt);
    scan.start(5000);
    if (!found) delay(1000);
  }
  return found;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(100);
  readName();

  // ===== Phase 1: Basic lifecycle =====
  waitForPhase(1);
  if (!phase_basic()) return;

  // ===== Phase 2: BLE5 ext adv + periodic =====
  waitForPhase(2);
  {
    bool ble5_ok = false;
#if BLE5_SUPPORTED
    ble5_ok = phase_ble5_scan();
#endif
    if (!ble5_ok) Serial.println("[CLIENT] BLE5 not supported, skipping");
  }

  // ===== Phase 3: Init for GATT tests =====
  waitForPhase(3);

  size_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[CLIENT] Heap before init: %u\n", (unsigned)heapBefore);

  BTStatus status = BLE.begin("BLE_CLT");
  if (!status) {
    Serial.printf("[CLIENT] GATT init FAILED: %s\n", status.toString());
    return;
  }
  Serial.println("[CLIENT] GATT init OK");
  BLE.setMTU(512);

  size_t heapAfterInit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[CLIENT] Heap after init: %u\n", (unsigned)heapAfterInit);

  BLESecurity sec = BLE.getSecurity();
  sec.setIOCapability(BLESecurity::DisplayYesNo);
  sec.setAuthenticationMode(true, true, true);
  sec.onConfirmPassKey([](const BLEConnInfo &conn, uint32_t passkey) -> bool {
    Serial.printf("[CLIENT] Passkey: %06lu\n", (unsigned long)passkey);
    return true;
  });
  sec.onAuthenticationComplete([](const BLEConnInfo &conn, bool success) {
    Serial.println("[CLIENT] Authentication complete");
  });

  if (!scanForServer()) {
    Serial.println("[CLIENT] Server not found");
    return;
  }

  uint32_t connectStart = millis();
  BLEClient client = BLE.createClient();
  status = client.connect(targetAddr);
  uint32_t connectTime = millis() - connectStart;
  if (!status) {
    Serial.printf("[CLIENT] Connect FAILED: %s\n", status.toString());
    return;
  }
  Serial.println("[CLIENT] Connected");
  Serial.printf("[CLIENT] Connect time: %lu ms\n", (unsigned long)connectTime);

  uint16_t mtu = client.getMTU();
  Serial.printf("[CLIENT] Negotiated MTU: %u\n", mtu);

  size_t heapAfterConnect = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[CLIENT] Heap after connect: %u\n", (unsigned)heapAfterConnect);

  BLERemoteService svc = client.getService(serviceUUID);
  if (!svc) {
    Serial.println("[CLIENT] Service not found");
    return;
  }
  Serial.println("[CLIENT] Found service");

  // ===== Phase 4: GATT read/write =====
  waitForPhase(4);

  BLERemoteCharacteristic rwChr = svc.getCharacteristic(rwCharUUID);
  if (!rwChr) {
    Serial.println("[CLIENT] RW characteristic not found");
    return;
  }
  Serial.println("[CLIENT] Found characteristic");

  uint64_t readStart = esp_timer_get_time();
  String val = rwChr.readValue();
  uint64_t readEnd = esp_timer_get_time();
  Serial.printf("[CLIENT] Read value: %s\n", val.c_str());
  Serial.printf("[CLIENT] Read latency: %lu us\n", (unsigned long)(readEnd - readStart));

  status = rwChr.writeValue("Hello from client!");
  if (status) {
    Serial.println("[CLIENT] Write OK");
  } else {
    Serial.printf("[CLIENT] Write FAILED: %s\n", status.toString());
  }

  val = rwChr.readValue();
  Serial.printf("[CLIENT] Read-back: %s\n", val.c_str());

  // ===== Phase 5: Notifications + Indications =====
  waitForPhase(5);

  BLERemoteCharacteristic notifyChr = svc.getCharacteristic(notifyCharUUID);
  BLERemoteCharacteristic indicateChr = svc.getCharacteristic(indicateCharUUID);

  volatile bool notifReceived = false;
  volatile bool indicReceived = false;

  notifyChr.subscribe(true, [&notifReceived](BLERemoteCharacteristic chr, const uint8_t *data,
                                size_t length, bool isNotify) {
    String v((const char *)data, length);
    Serial.printf("[CLIENT] Notification received: %s\n", v.c_str());
    notifReceived = true;
  });
  Serial.println("[CLIENT] Subscribed to notifications");

  // Wait for notification before subscribing to indications to ensure deterministic output order
  {
    unsigned long start = millis();
    while (!notifReceived && (millis() - start < 30000)) {
      delay(100);
    }
  }

  indicateChr.subscribe(false, [&indicReceived](BLERemoteCharacteristic chr, const uint8_t *data,
                                   size_t length, bool isNotify) {
    String v((const char *)data, length);
    Serial.printf("[CLIENT] Indication received: %s\n", v.c_str());
    indicReceived = true;
  });
  Serial.println("[CLIENT] Subscribed to indications");

  // Wait for indication
  {
    unsigned long start = millis();
    while (!indicReceived && (millis() - start < 30000)) {
      delay(100);
    }
  }

  notifyChr.unsubscribe();
  indicateChr.unsubscribe();
  Serial.println("[CLIENT] Unsubscribed");

  // ===== Phase 6: Large ATT write (>MTU) =====
  waitForPhase(6);
  {
    const size_t bigLen = 512;
    uint8_t bigBuf[bigLen];
    for (size_t i = 0; i < bigLen; i++) bigBuf[i] = (uint8_t)(i & 0xFF);

    status = rwChr.writeValue(bigBuf, bigLen);
    if (status) {
      Serial.println("[CLIENT] Large write OK");
    } else {
      Serial.printf("[CLIENT] Large write FAILED: %s\n", status.toString());
    }

    val = rwChr.readValue();
    Serial.printf("[CLIENT] Large read (%u bytes)\n", val.length());

    bool match = (val.length() == bigLen);
    if (match) {
      for (size_t i = 0; i < val.length(); i++) {
        if ((uint8_t)val[i] != (uint8_t)(i & 0xFF)) {
          match = false;
          break;
        }
      }
    }
    if (match) {
      Serial.println("[CLIENT] Large data integrity OK");
    } else {
      Serial.println("[CLIENT] Large data integrity FAILED");
    }
  }

  // ===== Phase 7: Descriptor read/write =====
  waitForPhase(7);
  {
    BLERemoteCharacteristic descChr = svc.getCharacteristic(descCharUUID);
    if (!descChr) {
      Serial.println("[CLIENT] Descriptor char not found");
    } else {
      Serial.println("[CLIENT] Found descriptor char");
      auto descriptors = descChr.getDescriptors();
      Serial.printf("[CLIENT] Descriptor count: %u\n", (unsigned)descriptors.size());

      // Find User Description (0x2901)
      BLERemoteDescriptor userDesc = descChr.getDescriptor(BLEUUID(static_cast<uint16_t>(0x2901)));
      if (userDesc) {
        String descVal = userDesc.readValue();
        Serial.printf("[CLIENT] User description: %s\n", descVal.c_str());
      } else {
        Serial.println("[CLIENT] User description not found");
      }

      // Find Presentation Format (0x2904)
      BLERemoteDescriptor pfDesc = descChr.getDescriptor(BLEUUID(static_cast<uint16_t>(0x2904)));
      if (pfDesc) {
        String pfVal = pfDesc.readValue();
        if (pfVal.length() >= 7) {
          uint8_t format = (uint8_t)pfVal[0];
          Serial.printf("[CLIENT] Presentation format: %u\n", format);
          if (format == BLEDescriptor::FORMAT_UTF8) {
            Serial.println("[CLIENT] Format matches UTF8");
          }
        }
      } else {
        Serial.println("[CLIENT] Presentation format not found");
      }
    }
  }

  // ===== Phase 8: Write without response =====
  waitForPhase(8);
  {
    BLERemoteCharacteristic writeNrChr = svc.getCharacteristic(writeNrCharUUID);
    if (!writeNrChr) {
      Serial.println("[CLIENT] WriteNR char not found");
    } else {
      Serial.println("[CLIENT] Found WriteNR char");
      status = writeNrChr.writeValue("WriteNR_OK", false);
      if (status) {
        Serial.println("[CLIENT] WriteNR sent");
      } else {
        Serial.printf("[CLIENT] WriteNR FAILED: %s\n", status.toString());
      }
      delay(500);
      // Read back to verify server received it
      String readBack = writeNrChr.readValue();
      Serial.printf("[CLIENT] WriteNR readback: %s\n", readBack.c_str());
    }
    Serial.println("[CLIENT] Status: write_no_response done");
  }

  // ===== Phase 9: Server-initiated disconnect =====
  waitForPhase(9);
  {
    Serial.println("[CLIENT] Waiting for server disconnect...");
    unsigned long start = millis();
    while (client.isConnected() && (millis() - start < 10000)) {
      delay(100);
    }
    if (!client.isConnected()) {
      Serial.println("[CLIENT] Server disconnected us");
    } else {
      Serial.println("[CLIENT] Server disconnect timeout");
    }

    // Reconnect after server-initiated disconnect
    delay(2000);
    client = BLE.createClient();
    status = client.connect(targetAddr);
    if (status) {
      Serial.println("[CLIENT] Reconnected after server disconnect");

      // Re-discover services
      svc = client.getService(serviceUUID);
      if (!svc) {
        Serial.println("[CLIENT] Service not found after reconnect");
      }
    } else {
      Serial.printf("[CLIENT] Reconnect FAILED: %s\n", status.toString());
    }
  }

  // ===== Phase 10: Security — encrypted characteristic =====
  waitForPhase(10);
  {
    BLERemoteCharacteristic secureChr = svc.getCharacteristic(secureCharUUID);
    delay(1000);
    val = secureChr.readValue();
    Serial.printf("[CLIENT] Secure read: %s\n", val.c_str());
  }

  // ===== Phase 11: BLE5 PHY + DLE =====
  waitForPhase(11);
  {
    bool phy_ok = false;
#if BLE5_SUPPORTED
    status = client.setPhy(BLEPhy::PHY_2M, BLEPhy::PHY_2M);
    if (status) {
      Serial.println("[CLIENT] PHY update OK");
      phy_ok = true;
    }
    if (phy_ok) {
      status = client.setDataLen(251, 2120);
      if (status) Serial.println("[CLIENT] DLE set OK");
      else Serial.printf("[CLIENT] DLE failed: %s\n", status.toString());
    }
#endif
    if (!phy_ok) Serial.println("[CLIENT] BLE5 PHY/DLE not supported, skipping");
  }

  // ===== Phase 12: Reconnect (3 cycles) =====
  waitForPhase(12);
  client.disconnect();
  Serial.println("[CLIENT] Disconnected for reconnect test");
  delay(2000);

  for (int i = 0; i < 3; i++) {
    Serial.printf("[CLIENT] Connect cycle %d\n", i + 1);
    BLEClient rc = BLE.createClient();
    status = rc.connect(targetAddr);
    if (status) {
      Serial.println("[CLIENT] Connected");
      delay(2000);
      rc.disconnect();
      Serial.println("[CLIENT] Disconnected");
      delay(2000);
    } else {
      Serial.printf("[CLIENT] Connect FAILED cycle %d\n", i + 1);
    }
  }

  Serial.println("[CLIENT] All cycles complete");

  // ===== Phase 13: BLEStream =====
  waitForPhase(13);
  {
    bool stream_ok = false;
    // Don't tear down BLE — BLEStream layers on the existing stack.
    // Give server time to reconfigure advertising with NUS service.
    delay(2000);
    BLEStream stream;
    for (int attempt = 1; attempt <= 5 && !stream_ok; attempt++) {
      BTStatus s = stream.beginClient(targetAddr, 7000);
      if (!s) {
        Serial.printf("[CLIENT] BLEStream connect attempt %d failed: %s\n", attempt, s.toString());
        delay(500);
        continue;
      }
      Serial.println("[CLIENT] BLEStream init OK");
      stream.println("stream_ping");
      Serial.println("[CLIENT] BLEStream sent");

      String rx;
      unsigned long start = millis();
      while (millis() - start < 10000) {
        while (stream.available()) {
          int c = stream.read();
          if (c < 0) break;
          if (c == '\n') {
            rx.trim();
            Serial.printf("[CLIENT] BLEStream received: %s\n", rx.c_str());
            if (rx == "STREAM_OK") {
              Serial.println("[CLIENT] Status: blestream done");
              stream_ok = true;
            }
            break;
          }
          rx += (char)c;
        }
        if (stream_ok) break;
        delay(20);
      }
      stream.end();
    }
    if (!stream_ok) {
      Serial.println("[CLIENT] BLEStream phase FAILED");
    }
  }

  // ===== Phase 14: L2CAP CoC =====
  waitForPhase(14);
  {
    bool l2cap_supported = false;
#if BLE_L2CAP_SUPPORTED
    l2cap_supported = true;
    BTStatus s;
    BLEClient l2capClient = BLE.createClient();
    s = l2capClient.connect(targetAddr);
    if (!s) {
      Serial.printf("[CLIENT] L2CAP connect FAILED: %s\n", s.toString());
    } else {
      Serial.println("[CLIENT] L2CAP init OK");
      BLEL2CAPChannel channel = BLE.connectL2CAP(l2capClient.getHandle(), 0x0080, 128);
      if (!channel) {
        Serial.println("[CLIENT] L2CAP open FAILED");
      } else {
        volatile bool dataReceived = false;
        String l2capRx;
        channel.onData([&](BLEL2CAPChannel ch, const uint8_t *data, size_t len) {
          l2capRx = String((const char *)data, len);
          dataReceived = true;
        });

        unsigned long connectStart = millis();
        while (!channel.isConnected() && (millis() - connectStart < 10000)) {
          delay(20);
        }
        if (channel.isConnected()) {
          Serial.println("[CLIENT] L2CAP channel connected");
          const char *payload = "L2CAP_PING";
          s = channel.write((const uint8_t *)payload, strlen(payload));
          if (s) {
            Serial.println("[CLIENT] L2CAP sent");
          } else {
            Serial.printf("[CLIENT] L2CAP write FAILED: %s\n", s.toString());
          }

          unsigned long rxStart = millis();
          while (!dataReceived && (millis() - rxStart < 10000)) {
            delay(20);
          }
          if (dataReceived) {
            Serial.printf("[CLIENT] L2CAP received: %s\n", l2capRx.c_str());
            if (l2capRx == "L2CAP_OK") {
              Serial.println("[CLIENT] Status: l2cap done");
            }
          } else {
            Serial.println("[CLIENT] L2CAP receive timeout");
          }
          channel.disconnect();
        } else {
          Serial.println("[CLIENT] L2CAP channel connect timeout");
        }
      }
      l2capClient.disconnect();
    }
#endif
    if (!l2cap_supported) {
      Serial.println("[CLIENT] L2CAP not supported, skipping");
    }
  }

  // ===== Phase 15: Memory release + reinit guard =====
  waitForPhase(15);

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
