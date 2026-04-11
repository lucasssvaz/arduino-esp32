// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "esp32-hal-bt.h"

#if SOC_BT_SUPPORTED
#if (defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED)) && __has_include("esp_bt.h")

// Flag set by constructors in esp32-hal-bt-mem.h when BT libraries are linked
bool _btLibraryInUse = false;

// Default behavior: release BTDM memory (~36KB) unless a BT library is used or user overrides.
// BT libraries include esp32-hal-bt-mem.h which sets _btLibraryInUse = true via constructor.
// Users can also provide their own strong btInUse() implementation.
__attribute__((weak)) bool btInUse(void) {
  return _btLibraryInUse;
}

#include "esp_bt.h"

#ifdef CONFIG_BTDM_CONTROLLER_MODE_BTDM
#define BT_MODE ESP_BT_MODE_BTDM
#elif defined(CONFIG_BTDM_CONTROLLER_MODE_BR_EDR_ONLY)
#define BT_MODE ESP_BT_MODE_CLASSIC_BT
#else
#define BT_MODE ESP_BT_MODE_BLE
#endif

static const char __attribute__((unused)) *btModeStr(esp_bt_mode_t mode) {
  switch (mode) {
    case ESP_BT_MODE_BLE:        return "BLE";
    case ESP_BT_MODE_CLASSIC_BT: return "Classic BT";
    case ESP_BT_MODE_BTDM:       return "BT Dual Mode";
    default:                     return "Unknown";
  }
}

static esp_bt_mode_t _btActiveMode = 0;

bool btStarted() {
  return (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED);
}

bool btStart() {
  return btStartMode(BT_MODE);
}

bool btStartMode(bt_mode mode) {
  esp_bt_mode_t esp_bt_mode;
  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#if CONFIG_IDF_TARGET_ESP32
  switch (mode) {
    case BT_MODE_BLE:        esp_bt_mode = ESP_BT_MODE_BLE; break;
    case BT_MODE_CLASSIC_BT: esp_bt_mode = ESP_BT_MODE_CLASSIC_BT; break;
    case BT_MODE_BTDM:       esp_bt_mode = ESP_BT_MODE_BTDM; break;
    default:                 esp_bt_mode = BT_MODE; break;
  }
  cfg.mode = esp_bt_mode;
  if (cfg.mode == ESP_BT_MODE_CLASSIC_BT) {
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
  }
#else
  esp_bt_mode = BT_MODE;
#endif

  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    return true;
  }
  esp_err_t ret;
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
    if ((ret = esp_bt_controller_init(&cfg)) != ESP_OK) {
      log_e("BT controller init failed: %s (mode=%s)", esp_err_to_name(ret), btModeStr(esp_bt_mode));
      return false;
    }
    while (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {}
  }
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
    if ((ret = esp_bt_controller_enable(esp_bt_mode)) != ESP_OK) {
      log_e("BT controller enable failed: %s (mode=%s)", esp_err_to_name(ret), btModeStr(esp_bt_mode));
      return false;
    }
  }
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    _btActiveMode = esp_bt_mode;
    return true;
  }
  log_e("BT start failed: controller in unexpected state");
  return false;
}

bool btStop() {
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
    _btActiveMode = 0;
    return true;
  }
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    esp_err_t ret = esp_bt_controller_disable();
    if (ret != ESP_OK) {
      log_e("BT controller disable failed: %s", esp_err_to_name(ret));
      return false;
    }
    while (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED);
  }
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
    esp_err_t ret = esp_bt_controller_deinit();
    if (ret != ESP_OK) {
      log_e("BT controller deinit failed: %s", esp_err_to_name(ret));
      return false;
    }
    vTaskDelay(1);
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
      log_e("BT stop failed: controller did not reach idle state");
      return false;
    }
    _btActiveMode = 0;
    return true;
  }
  log_e("BT stop failed: controller in unexpected state");
  return false;
}

bt_mode btGetMode(void) {
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
    return BT_MODE_DEFAULT;
  }
  switch (_btActiveMode) {
    case ESP_BT_MODE_BLE:        return BT_MODE_BLE;
    case ESP_BT_MODE_CLASSIC_BT: return BT_MODE_CLASSIC_BT;
    case ESP_BT_MODE_BTDM:       return BT_MODE_BTDM;
    default:                     return BT_MODE_DEFAULT;
  }
}

#else  // !__has_include("esp_bt.h") || !(defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED))
bool btStarted() {
  return false;
}

bool btStart() {
  return false;
}

bool btStop() {
  return false;
}

bt_mode btGetMode(void) {
  return BT_MODE_DEFAULT;
}

#endif /* !__has_include("esp_bt.h") || !(defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED)) */

#endif /* SOC_BT_SUPPORTED */
