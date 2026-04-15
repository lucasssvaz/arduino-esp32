// Combined BLE validation test — SERVER
// Phases: basic lifecycle, BLE5 ext+periodic adv, GATT server, notifications,
//         large writes, descriptors, write-no-response, server disconnect,
//         security, reconnect

#include <Arduino.h>
#include <BLE.h>
#include "esp_heap_caps.h"

#define SERVICE_UUID       "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define RW_CHAR_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define NOTIFY_CHAR_UUID   "cba1d466-344c-4be3-ab3f-189f80dd7518"
#define INDICATE_CHAR_UUID "d5f782b2-a36e-4d68-947c-0e9a5f2c78e1"
#define SECURE_CHAR_UUID   "ff1d2614-e2d6-4c87-9154-6625d39ca7f8"
#define DESC_CHAR_UUID     "a3c87501-8ed3-4bdf-8a39-a01bebede295"
#define WRITENR_CHAR_UUID  "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"

String serverName;
BLECharacteristic notifyChr;
BLECharacteristic indicateChr;
int notifyStep = 0;
volatile int currentPhase = 0;
bool serverDisconnectDone = false;
bool phase15Done = false;

BLEStream bleStream;
bool bleStreamInitDone = false;
bool bleStreamPhaseDone = false;
int bleStreamStep = 0;
String bleStreamRxLine;
volatile bool bleStreamConnectCb = false;
volatile bool bleStreamDisconnectCb = false;
bool bleStreamPeekDone = false;
bool l2capInitDone = false;

#if BLE_L2CAP_SUPPORTED
BLEL2CAPServer l2capServer;
BLEL2CAPChannel l2capChannel;
#endif

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
          Serial.printf("[SERVER] Phase %d started\n", phase);
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
  Serial.println("[SERVER] Device ready for name");
  Serial.println("[SERVER] Send name:");
  while (serverName.length() == 0) {
    if (Serial.available()) {
      serverName = Serial.readStringUntil('\n');
      serverName.trim();
    }
    delay(100);
  }
  Serial.printf("[SERVER] Name: %s\n", serverName.c_str());
}

// ========================= Phase 1 — Basic Lifecycle =========================

bool phase_basic() {
  BTStatus status = BLE.begin(serverName);
  if (!status) {
    Serial.printf("[SERVER] Init FAILED: %s\n", status.toString());
    return false;
  }
  Serial.printf("[SERVER] Stack: %s\n", BLE.getStackName());
  Serial.println("[SERVER] Init OK");
  Serial.printf("[SERVER] Device name: %s\n", BLE.getDeviceName().c_str());
  Serial.printf("[SERVER] Address: %s\n", BLE.getAddress().toString().c_str());

  BLE.end(false);
  Serial.println("[SERVER] Deinit OK");
  delay(1000);

  status = BLE.begin(serverName + "_reinit");
  if (!status) {
    Serial.printf("[SERVER] Reinit FAILED: %s\n", status.toString());
    return false;
  }
  Serial.println("[SERVER] Reinit OK");

  BLE.end(false);
  Serial.println("[SERVER] Final deinit OK");
  return true;
}

// ============== Phase 2 — BLE5 Extended + Periodic Advertising ===============

#if BLE5_SUPPORTED
bool phase_ble5_adv() {
  BTStatus status = BLE.begin(serverName);
  if (!status) return false;

  BLEAdvertising adv = BLE.getAdvertising();

  BLEAdvertising::ExtAdvConfig extCfg;
  extCfg.instance = 0;
  extCfg.type = BLEAdvType::NonConnectable;
  extCfg.primaryPhy = BLEPhy::PHY_1M;
  extCfg.secondaryPhy = BLEPhy::PHY_2M;
  extCfg.sid = 1;
  status = adv.configureExtended(extCfg);
  if (!status) {
    BLE.end(false);
    return false;
  }

  Serial.println("[SERVER] BLE5 init OK");
  Serial.println("[SERVER] Extended adv configured");

  BLEAdvertisementData extData;
  extData.setName(serverName);
  adv.setExtAdvertisementData(0, extData);

  BLEAdvertising::PeriodicAdvConfig perCfg;
  perCfg.instance = 0;
  perCfg.intervalMin = 0x20;
  perCfg.intervalMax = 0x40;
  adv.configurePeriodicAdv(perCfg);

  BLEAdvertisementData perData;
  perData.setName("PeriodicPayload");
  adv.setPeriodicAdvData(0, perData);

  status = adv.startExtended(0, 0, 0);
  if (!status) {
    BLE.end(false);
    return false;
  }
  Serial.println("[SERVER] Extended adv started");

  adv.startPeriodicAdv(0);
  Serial.println("[SERVER] Periodic adv started");

  delay(20000);

  adv.stopPeriodicAdv(0);
  adv.stopExtended(0);
  BLE.end(false);
  Serial.println("[SERVER] BLE5 adv phase done");
  return true;
}
#endif

// ====================== Phase 3 — GATT Server Setup =========================

bool phase_gatt_setup() {
  size_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[SERVER] Heap before init: %u\n", (unsigned)heapBefore);

  BTStatus status = BLE.begin(serverName);
  if (!status) {
    Serial.println("[SERVER] GATT init FAILED");
    return false;
  }
  Serial.println("[SERVER] GATT init OK");
  BLE.setMTU(512);

  size_t heapAfterInit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[SERVER] Heap after init: %u\n", (unsigned)heapAfterInit);

  BLESecurity sec = BLE.getSecurity();
  sec.setIOCapability(BLESecurity::DisplayYesNo);
  sec.setAuthenticationMode(true, true, true);
  sec.onConfirmPassKey([](const BLEConnInfo &conn, uint32_t passkey) -> bool {
    Serial.printf("[SERVER] Passkey: %06lu\n", (unsigned long)passkey);
    return true;
  });
  sec.onAuthenticationComplete([](const BLEConnInfo &conn, bool success) {
    Serial.println("[SERVER] Authentication complete");
  });
  Serial.println("[SERVER] Security configured");

  BLEServer server = BLE.createServer();
  server.onConnect([](BLEServer s, const BLEConnInfo &conn) {
    Serial.println("[SERVER] Client connected");
  });
  server.onDisconnect([](BLEServer s, const BLEConnInfo &conn, uint8_t reason) {
    Serial.println("[SERVER] Client disconnected");
  });
  server.advertiseOnDisconnect(true);

  BLEService svc = server.createService(BLEUUID(SERVICE_UUID));

  auto rwChr = svc.createCharacteristic(BLEUUID(RW_CHAR_UUID),
    BLEProperty::Read | BLEProperty::Write);
  rwChr.setValue("Hello from server!");
  rwChr.onWrite([](BLECharacteristic c, const BLEConnInfo &conn) {
    size_t len = 0;
    const uint8_t *data = c.getValue(&len);
    if (len <= 50) {
      Serial.printf("[SERVER] Received write: %.*s\n", (int)len, (const char *)data);
    }
    Serial.printf("[SERVER] Received %u bytes\n", (unsigned)len);
  });

  notifyChr = svc.createCharacteristic(BLEUUID(NOTIFY_CHAR_UUID),
    BLEProperty::Read | BLEProperty::Notify);
  notifyChr.onSubscribe([](BLECharacteristic chr, const BLEConnInfo &conn, uint16_t subValue) {
    Serial.printf("[SERVER] Subscriber count: %u\n", chr.getSubscribedCount());
  });

  indicateChr = svc.createCharacteristic(BLEUUID(INDICATE_CHAR_UUID),
    BLEProperty::Read | BLEProperty::Indicate);

  auto secureChr = svc.createCharacteristic(BLEUUID(SECURE_CHAR_UUID),
    BLEProperty::Read);
  secureChr.setPermissions(BLEPermission::ReadEncrypted | BLEPermission::ReadAuthenticated);
  secureChr.setValue("Secure Data!");

  // Phase 7: Descriptor test characteristic with User Description + Presentation Format
  auto descChr = svc.createCharacteristic(BLEUUID(DESC_CHAR_UUID),
    BLEProperty::Read | BLEProperty::Write);
  descChr.setValue("DescTest");
  descChr.setDescription("Test Characteristic");

  auto pfDesc = descChr.createDescriptor(BLEUUID(static_cast<uint16_t>(0x2904)), BLEPermission::Read, 7);
  uint8_t pfData[7] = {0};
  pfData[0] = BLEDescriptor::FORMAT_UTF8;  // format
  pfData[1] = 0;                           // exponent
  pfData[2] = 0x00; pfData[3] = 0x27;     // unit = 0x2700 (unitless)
  pfData[4] = 1;                           // namespace (Bluetooth SIG)
  pfData[5] = 0; pfData[6] = 0;           // description
  pfDesc.setValue(pfData, 7);

  // Phase 8: WriteNR test characteristic
  auto writeNrChr = svc.createCharacteristic(BLEUUID(WRITENR_CHAR_UUID),
    BLEProperty::Read | BLEProperty::WriteNR);
  writeNrChr.setValue("waiting");
  writeNrChr.onWrite([](BLECharacteristic c, const BLEConnInfo &conn) {
    size_t len = 0;
    const uint8_t *data = c.getValue(&len);
    Serial.printf("[SERVER] WriteNR received: %.*s\n", (int)len, (const char *)data);
  });

  svc.start();
  server.start();

  size_t heapAfterServer = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[SERVER] Heap after server: %u\n", (unsigned)heapAfterServer);
  Serial.println("[SERVER] Server started");

  BLEAdvertising adv = BLE.getAdvertising();
  adv.addServiceUUID(BLEUUID(SERVICE_UUID));
  adv.start();
  Serial.println("[SERVER] Advertising started");
  return true;
}

// =============================================================================

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(100);
  readName();

  waitForPhase(1);
  if (!phase_basic()) return;

  waitForPhase(2);
  {
    bool ble5_ok = false;
#if BLE5_SUPPORTED
    ble5_ok = phase_ble5_adv();
#endif
    if (!ble5_ok) Serial.println("[SERVER] BLE5 not supported, skipping");
  }

  waitForPhase(3);
  if (!phase_gatt_setup()) return;
}

void loop() {
  checkSerial();

  static unsigned long notifyTime = 0;

  // Phase 5: Send notification then indication
  if (currentPhase >= 5 && notifyStep == 0 && notifyChr.getSubscribedCount() > 0) {
    notifyChr.setValue("notify_test_1");
    BTStatus s = notifyChr.notify();
    if (s) Serial.println("[SERVER] Notification sent");
    notifyStep = 1;
    notifyTime = millis();
  }
  if (notifyStep == 1 && millis() - notifyTime >= 1000) {
    indicateChr.setValue("indicate_test_1");
    BTStatus s = indicateChr.indicate();
    if (s) Serial.println("[SERVER] Indication sent");
    notifyStep = 2;
  }

  // Phase 9: Server-initiated disconnect
  if (currentPhase >= 9 && !serverDisconnectDone) {
    serverDisconnectDone = true;
    BLEServer server = BLE.createServer();
    auto conns = server.getConnections();
    if (!conns.empty()) {
      BTStatus s = server.disconnect(conns[0].getHandle());
      if (s) {
        Serial.println("[SERVER] Server-initiated disconnect OK");
      } else {
        Serial.println("[SERVER] Server-initiated disconnect FAILED");
      }
    }
  }

  // Phase 13: BLEStream (server side)
  if (currentPhase >= 13 && !bleStreamInitDone) {
    bleStreamInitDone = true;
    bleStream.onConnect([]() {
      bleStreamConnectCb = true;
    });
    bleStream.onDisconnect([]() {
      bleStreamDisconnectCb = true;
    });
    BTStatus s = bleStream.begin(serverName);
    if (s) {
      Serial.println("[SERVER] BLEStream init OK");
    } else {
      Serial.printf("[SERVER] BLEStream init FAILED: %s\n", s.toString());
    }
  }

  if (currentPhase >= 13 && bleStreamInitDone && !bleStreamPhaseDone) {
    // Step 0: Wait for onConnect callback
    if (bleStreamStep == 0 && bleStreamConnectCb) {
      Serial.println("[SERVER] BLEStream onConnect fired");
      if (bleStream.connected()) {
        Serial.println("[SERVER] BLEStream connected OK");
      }
      bleStreamStep = 1;
    }

    // Step 1: Receive "stream_ping", reply "STREAM_PONG"
    if (bleStreamStep == 1 && bleStream.available()) {
      while (bleStream.available()) {
        int c = bleStream.read();
        if (c < 0) break;
        if (c == '\n') {
          bleStreamRxLine.trim();
          Serial.printf("[SERVER] BLEStream received: %s\n", bleStreamRxLine.c_str());
          bleStream.println("STREAM_PONG");
          Serial.println("[SERVER] BLEStream reply sent");
          bleStreamRxLine = "";
          bleStreamStep = 2;
          break;
        }
        bleStreamRxLine += (char)c;
      }
    }

    // Step 2: peek() test — peek first byte, then read full "peek_test" line
    if (bleStreamStep == 2 && bleStream.available()) {
      if (!bleStreamPeekDone) {
        int p = bleStream.peek();
        Serial.printf("[SERVER] BLEStream peek: %d\n", p);
        bleStreamPeekDone = true;
      }
      while (bleStream.available()) {
        int c = bleStream.read();
        if (c < 0) break;
        if (c == '\n') {
          bleStreamRxLine.trim();
          Serial.printf("[SERVER] BLEStream peek read: %s\n", bleStreamRxLine.c_str());
          bleStreamRxLine = "";
          bleStreamStep = 3;
          break;
        }
        bleStreamRxLine += (char)c;
      }
    }

    // Step 3: Receive bulk data (200-char pattern), verify integrity
    if (bleStreamStep == 3 && bleStream.available()) {
      while (bleStream.available()) {
        int c = bleStream.read();
        if (c < 0) break;
        if (c == '\n') {
          bleStreamRxLine.trim();
          size_t len = bleStreamRxLine.length();
          Serial.printf("[SERVER] BLEStream bulk received: %u bytes\n", (unsigned)len);
          bool ok = (len == 200);
          for (size_t i = 0; i < len && ok; i++) {
            if (bleStreamRxLine[i] != (char)('A' + (i % 26))) {
              ok = false;
            }
          }
          Serial.println(ok ? "[SERVER] BLEStream bulk integrity OK"
                             : "[SERVER] BLEStream bulk integrity FAILED");
          bleStreamRxLine = "";
          bleStreamStep = 4;
          break;
        }
        bleStreamRxLine += (char)c;
      }
    }

    // Step 4: Server-initiated write to client
    if (bleStreamStep == 4) {
      bleStream.println("server_says_hi");
      Serial.println("[SERVER] BLEStream server msg sent");
      bleStreamStep = 5;
    }

    // Step 5: Wait for onDisconnect callback
    if (bleStreamStep == 5 && bleStreamDisconnectCb) {
      Serial.println("[SERVER] BLEStream onDisconnect fired");
      bleStreamPhaseDone = true;
      bleStream.end();
    }
  }

  // Phase 14: L2CAP CoC (server side)
  if (currentPhase >= 14 && !l2capInitDone) {
#if BLE_L2CAP_SUPPORTED
    l2capInitDone = true;
    l2capServer = BLE.createL2CAPServer(0x0080, 128);
    if (!l2capServer) {
      Serial.println("[SERVER] L2CAP init FAILED");
    } else {
      l2capServer.onAccept([](BLEL2CAPChannel channel) {
        l2capChannel = channel;
        Serial.println("[SERVER] L2CAP channel accepted");
      });
      l2capServer.onData([](BLEL2CAPChannel channel, const uint8_t *data, size_t len) {
        String msg((const char *)data, len);
        Serial.printf("[SERVER] L2CAP received: %s\n", msg.c_str());
        const char *reply = "L2CAP_OK";
        BTStatus s = channel.write((const uint8_t *)reply, strlen(reply));
        if (s) {
          Serial.println("[SERVER] L2CAP reply sent");
        } else {
          Serial.printf("[SERVER] L2CAP reply FAILED: %s\n", s.toString());
        }
      });
      Serial.println("[SERVER] L2CAP init OK");
    }
#else
    l2capInitDone = true;
    Serial.println("[SERVER] L2CAP not supported, skipping");
#endif
  }

  // Phase 15: Memory release
  if (currentPhase >= 15 && !phase15Done) {
    phase15Done = true;
    delay(500);

    size_t heapBeforeRelease = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    Serial.printf("[SERVER] Heap before release: %u\n", (unsigned)heapBeforeRelease);

    BLE.end(true);

    delay(500);
    size_t heapAfterRelease = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    Serial.printf("[SERVER] Heap after release: %u\n", (unsigned)heapAfterRelease);

    int32_t freed = (int32_t)heapAfterRelease - (int32_t)heapBeforeRelease;
    Serial.printf("[SERVER] Memory freed: %ld bytes\n", (long)freed);
    if (freed >= 10240) {
      Serial.println("[SERVER] Memory release OK");
    } else {
      Serial.println("[SERVER] Memory release INSUFFICIENT");
    }

    BTStatus s = BLE.begin("ShouldFail");
    if (!s) {
      Serial.println("[SERVER] Reinit blocked OK");
    } else {
      Serial.println("[SERVER] Reinit was NOT blocked");
    }

    Serial.println("[SERVER] All phases complete");
  }

  delay(50);
}
