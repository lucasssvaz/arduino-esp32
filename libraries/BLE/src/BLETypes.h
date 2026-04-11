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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)

/**
 * @brief Convenience macros for BLE availability checks.
 *
 * Use these instead of repeating the full #if expressions:
 *   BLE_SUPPORTED      -- BLE hardware or hosted BLE is present
 *   BLE_STACK_ENABLED  -- A BLE host stack (NimBLE or Bluedroid) is configured
 *   BLE5_SUPPORTED     -- BLE 5.0 features are available (native or hosted)
 */
#if !defined(BLE_SUPPORTED)
#define BLE_SUPPORTED 1
#endif

#if (defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED)) && !defined(BLE_STACK_ENABLED)
#define BLE_STACK_ENABLED 1
#endif

#if (defined(SOC_BLE_50_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)) && !defined(BLE5_SUPPORTED)
#define BLE5_SUPPORTED 1
#endif

#include "BTAddress.h"
#include "BTStatus.h"
#include "BLEUUID.h"
#include "BLEProperty.h"
#include "BLEAdvTypes.h"
#include "BLEConnInfo.h"

#endif /* SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE */
