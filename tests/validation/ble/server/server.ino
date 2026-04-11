// Combined BLE validation test — SERVER
// Phases: basic lifecycle, BLE5 ext+periodic adv, GATT server, notifications,
//         large writes, security, reconnect

#include <Arduino.h>
#include <BLE.h>
#include "esp_heap_caps.h"

#define SERVICE_UUID       "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define RW_CHAR_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define NOTIFY_CHAR_UUID   "cba1d466-344c-4be3-ab3f-189f80dd7518"
#define INDICATE_CHAR_UUID "d5f782b2-a36e-4d68-947c-0e9a5f2c78e1"
#define SECURE_CHAR_UUID   "ff1d2614-e2d6-4c87-9154-6625d39ca7f8"

String serverName;
BLECharacteristic notifyChr;
BLECharacteristic indicateChr;
volatile int notifyStep = 0;
volatile int disconnectCount = 0;

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
    disconnectCount++;
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

  if (!phase_basic()) return;
  delay(1000);

  {
    bool ble5_ok = false;
#if BLE5_SUPPORTED
    ble5_ok = phase_ble5_adv();
    delay(1000);
#endif
    if (!ble5_ok) Serial.println("[SERVER] BLE5 not supported, skipping");
  }

  if (!phase_gatt_setup()) return;
}

void loop() {
  static unsigned long lastAction = 0;
  static bool phase10Done = false;

  // Phase 10: Memory release + reinit guard (after all reconnect cycles)
  if (disconnectCount >= 4 && !phase10Done) {
    phase10Done = true;
    delay(2000);

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
    return;
  }

  if (millis() - lastAction < 2000) return;
  lastAction = millis();

  if (notifyChr.getSubscribedCount() > 0 && notifyStep == 0) {
    notifyChr.setValue("notify_test_1");
    BTStatus s = notifyChr.notify();
    if (s) Serial.println("[SERVER] Notification sent");
    notifyStep = 1;
  } else if (notifyStep == 1) {
    indicateChr.setValue("indicate_test_1");
    BTStatus s = indicateChr.indicate();
    if (s) Serial.println("[SERVER] Indication sent");
    notifyStep = 2;
  }
}
