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

/**
 * @brief Central BLE compilation guards.
 *
 * Include this header instead of manually writing out the long
 * SOC_BLE_SUPPORTED / CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE / CONFIG_NIMBLE_ENABLED
 * preprocessor checks.
 *
 * Macros provided:
 *   BLE_NIMBLE         – NimBLE backend is active
 *   BLE_BLUEDROID      – Bluedroid backend is active
 *   BLE_ENABLED        – BLE is available AND a stack (NimBLE or Bluedroid) is enabled
 *   BLE5_SUPPORTED     – BLE 5.0 features are available (extended adv, PHY, etc.)
 *   BLE_L2CAP_SUPPORTED – L2CAP CoC channels are available (NimBLE + config)
 */

/* NimBLE stack selected on a BLE-capable target (native SoC or ESP-Hosted) */
#if (defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)) && defined(CONFIG_NIMBLE_ENABLED)
#define BLE_NIMBLE 1
#else
#define BLE_NIMBLE 0
#endif

/* Bluedroid stack selected (native SoC only, not available via ESP-Hosted) */
#if defined(SOC_BLE_SUPPORTED) && defined(CONFIG_BLUEDROID_ENABLED)
#define BLE_BLUEDROID 1
#else
#define BLE_BLUEDROID 0
#endif

/* BLE is usable: hardware is present AND a BT stack is configured */
#if BLE_NIMBLE || BLE_BLUEDROID
#define BLE_ENABLED 1
#else
#define BLE_ENABLED 0
#endif

/* BLE 5.0 features (extended advertising, 2M/Coded PHY, periodic adv, …) */
#if BLE_NIMBLE && CONFIG_BT_NIMBLE_EXT_ADV
#define BLE5_SUPPORTED 1
#elif BLE_BLUEDROID && defined(SOC_BLE_50_SUPPORTED) && defined(CONFIG_BT_BLE_50_FEATURES_SUPPORTED)
#define BLE5_SUPPORTED 1
#else
#define BLE5_SUPPORTED 0
#endif

/* L2CAP Connection-Oriented Channels (CoC) — NimBLE only, requires config */
#if BLE_NIMBLE && defined(CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM) && (CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM > 0)
#define BLE_L2CAP_SUPPORTED 1
#else
#define BLE_L2CAP_SUPPORTED 0
#endif
