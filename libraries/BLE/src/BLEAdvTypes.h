/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
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

#include <stdint.h>

/** @brief PHY types for BLE 5.0 connections and advertising. */
enum class BLEPhy : uint8_t {
  PHY_1M = 1,
  PHY_2M = 2,
  PHY_Coded = 3,
};

/** @brief Advertising type. */
enum class BLEAdvType : uint8_t {
  Connectable,
  ConnectableScannable,
  ConnectableDirected,
  ScannableUndirected,
  NonConnectable,
  DirectedHighDuty,
  DirectedLowDuty,
};

/**
 * @brief Connection parameters -- Bluetooth Core Spec Vol 6, Part B, 4.5.1.
 *
 * All interval/timeout values are in the units specified by the Bluetooth spec.
 */
struct BLEConnParams {
  uint16_t minInterval;  ///< 6..3200 (7.5ms..4s in 1.25ms units)
  uint16_t maxInterval;  ///< 6..3200
  uint16_t latency;      ///< 0..499
  uint16_t timeout;      ///< 10..3200 (100ms..32s in 10ms units)

  /**
   * @brief Validates parameters against Bluetooth Core Spec Vol 6, Part B, Section 4.5.1.
   * @return true if all parameters are within spec-mandated ranges.
   */
  bool isValid() const;
};

#endif /* BLE_ENABLED */
