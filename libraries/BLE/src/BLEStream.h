/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "impl/BLEGuards.h"
#if BLE_ENABLED

#include <Stream.h>
#include "BTStatus.h"
#include "BTAddress.h"
#include "BLEServer.h"
#include "BLEClient.h"
#include "BLECharacteristic.h"
#include "BLERemoteCharacteristic.h"
#include "BLEUUID.h"
#include <memory>
#include <functional>

/**
 * @brief Arduino Stream interface over BLE using the Nordic UART Service (NUS).
 *
 * BLEStream wraps BLE GATT operations behind the familiar Arduino Stream API.
 * It uses the Nordic UART Service (NUS) UUID conventions for maximum
 * interoperability with BLE serial apps (nRF Connect, Serial Bluetooth
 * Terminal, etc.).
 *
 * **Server mode** — the ESP32 advertises a NUS service and accepts connections:
 * @code
 * BLEStream bleStream;
 * bleStream.begin("MyDevice");          // starts advertising
 * if (bleStream.connected()) {
 *   bleStream.println("Hello World!");
 *   if (bleStream.available()) {
 *     String s = bleStream.readStringUntil('\n');
 *   }
 * }
 * @endcode
 *
 * **Client mode** — the ESP32 connects to a remote NUS server:
 * @code
 * BLEStream bleStream;
 * bleStream.beginClient(remoteAddr);    // connects to the remote device
 * bleStream.println("Hello Server!");
 * @endcode
 *
 * Stack-agnostic: works with both NimBLE and Bluedroid backends.
 */
class BLEStream : public Stream {
public:
  BLEStream(size_t rxBufferSize = 256);
  ~BLEStream();

  // Non-copyable, non-movable (owns server/client resources)
  BLEStream(const BLEStream &) = delete;
  BLEStream &operator=(const BLEStream &) = delete;

  /**
   * @brief Start as a NUS server with advertising.
   * @param deviceName BLE device name (used for BLE.begin and advertising)
   * @return BTStatus::OK on success
   */
  BTStatus begin(const String &deviceName = "BLE_Stream");

  /**
   * @brief Start as a NUS client and connect to a remote NUS server.
   * @param address BLE address of the remote NUS server
   * @param timeoutMs Connection timeout in milliseconds
   * @return BTStatus::OK on success
   */
  BTStatus beginClient(const BTAddress &address, uint32_t timeoutMs = 5000);

  /**
   * @brief Shut down the stream (stop advertising, disconnect, clean up).
   */
  void end();

  /**
   * @brief Check if a remote device is connected.
   */
  bool connected() const;

  // --- Arduino Stream interface ---
  int available() override;
  int read() override;
  int peek() override;
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  void flush() override;

  // --- Callbacks ---
  using ConnectHandler = std::function<void()>;
  using DisconnectHandler = std::function<void()>;

  void onConnect(ConnectHandler handler);
  void onDisconnect(DisconnectHandler handler);

  // --- NUS UUIDs (Nordic UART Service) ---
  static BLEUUID nusServiceUUID();
  static BLEUUID nusRxCharUUID();
  static BLEUUID nusTxCharUUID();

private:
  struct Impl;
  Impl *_impl;
};

#endif /* BLE_ENABLED */
