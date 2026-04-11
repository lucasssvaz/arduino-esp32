/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2018 pcbreflux
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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)

#include "BLEUUID.h"
#include "BLEAdvertisementData.h"
#include "WString.h"

/**
 * @brief Eddystone URL frame builder/parser.
 */
class BLEEddystoneURL {
public:
  BLEEddystoneURL();

  void setURL(const String &url);
  String getURL() const;

  void setTxPower(int8_t txPower);
  int8_t getTxPower() const;

  BLEAdvertisementData getAdvertisementData() const;

  void setFromServiceData(const uint8_t *data, size_t len);

  static BLEUUID serviceUUID();

private:
  int8_t _txPower = -20;
  String _url;

  static uint8_t encodePrefix(const String &url, size_t &consumed);
  static uint8_t encodeSuffix(const String &url, size_t pos, size_t &consumed);
  static String decodePrefix(uint8_t code);
  static String decodeSuffix(uint8_t code);
};

/**
 * @brief Eddystone TLM (telemetry) frame builder/parser.
 */
class BLEEddystoneTLM {
public:
  BLEEddystoneTLM();

  void setBatteryVoltage(uint16_t millivolts);
  uint16_t getBatteryVoltage() const;

  void setTemperature(float celsius);
  float getTemperature() const;

  void setAdvertisingCount(uint32_t count);
  uint32_t getAdvertisingCount() const;

  void setUptime(uint32_t deciseconds);
  uint32_t getUptime() const;

  BLEAdvertisementData getAdvertisementData() const;

  void setFromServiceData(const uint8_t *data, size_t len);

  String toString() const;

  static BLEUUID serviceUUID();

private:
  uint16_t _voltage = 0;
  int16_t _rawTemp = 0;
  uint32_t _advCount = 0;
  uint32_t _uptime = 0;
};

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
