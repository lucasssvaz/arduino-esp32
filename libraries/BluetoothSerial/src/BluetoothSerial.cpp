// Copyright 2018 Evandro Luis Copercini
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sdkconfig.h"
#include "soc/soc_caps.h"
#if SOC_BT_SUPPORTED && defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)

#include "BluetoothSerial.h"
#include "esp32-hal-bt.h"
#include "esp32-hal-bt-mem.h"
#include "esp32-hal-log.h"

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <esp_spp_api.h>
#include <cstring>

static constexpr uint16_t RX_QUEUE_SIZE = 512;
static constexpr uint16_t TX_QUEUE_SIZE = 32;
static constexpr uint16_t SPP_TX_MAX = 330;
static constexpr const char *DEFAULT_SPP_NAME = "ESP32SPP";

// Event group bits for SPP state
static constexpr EventBits_t SPP_RUNNING = BIT0;
static constexpr EventBits_t SPP_CONNECTED = BIT1;
static constexpr EventBits_t SPP_CONGESTED = BIT2;
static constexpr EventBits_t SPP_DISCONNECTED = BIT3;

// Event group bits for BT discovery state
static constexpr EventBits_t BT_DISCOVERY_RUNNING = BIT0;
static constexpr EventBits_t BT_DISCOVERY_COMPLETED = BIT1;

struct spp_packet_t {
  size_t len;
  uint8_t data[];
};

// --------------------------------------------------------------------------
// BluetoothSerial::Impl -- all state is instance-owned
// --------------------------------------------------------------------------

struct BluetoothSerial::Impl {
  // SPP state
  uint32_t sppClient = 0;
  QueueHandle_t rxQueue = nullptr;
  QueueHandle_t txQueue = nullptr;
  SemaphoreHandle_t txDone = nullptr;
  EventGroupHandle_t sppEventGroup = nullptr;
  EventGroupHandle_t btEventGroup = nullptr;
  TaskHandle_t txTaskHandle = nullptr;

  String localName;
  bool isInitiator = false;
  bool initialized = false;
  bool sspEnabled = false;
  bool ioCapInput = false;
  bool ioCapOutput = false;

  uint8_t pinCode[16] = {};
  uint8_t pinCodeLen = 0;

  uint8_t txBuffer[SPP_TX_MAX] = {};
  size_t txBufferLen = 0;

  // Callbacks
  std::function<void(const uint8_t *, size_t)> dataCb;
  std::function<void(uint32_t)> confirmRequestCb;
  std::function<void(bool)> authCompleteCb;
  std::function<void(const BluetoothSerial::DiscoveryResult &)> discoveryCb;

  // Discovery results
  std::vector<BluetoothSerial::DiscoveryResult> discoveryResults;

  // Connection target for name-based connect
  String targetName;
  uint8_t peerAddr[6] = {};
  bool isRemoteAddressSet = false;
  bool doConnect = false;

  bool createResources();
  void destroyResources();

  static void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
  static void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
  static void txTask(void *arg);
  static bool getNameFromEIR(const uint8_t *eir, char *bdname, uint8_t *bdname_len);
};

static BluetoothSerial::Impl *s_impl = nullptr;

bool BluetoothSerial::Impl::createResources() {
  rxQueue = xQueueCreate(RX_QUEUE_SIZE, sizeof(uint8_t));
  txQueue = xQueueCreate(TX_QUEUE_SIZE, sizeof(spp_packet_t *));
  txDone = xSemaphoreCreateBinary();
  sppEventGroup = xEventGroupCreate();
  btEventGroup = xEventGroupCreate();

  if (!rxQueue || !txQueue || !txDone || !sppEventGroup || !btEventGroup) {
    destroyResources();
    return false;
  }

  BaseType_t ret = xTaskCreatePinnedToCore(txTask, "spp_tx", 4096, this, configMAX_PRIORITIES - 1, &txTaskHandle, 0);
  if (ret != pdPASS) {
    destroyResources();
    return false;
  }
  return true;
}

void BluetoothSerial::Impl::destroyResources() {
  if (txTaskHandle) {
    vTaskDelete(txTaskHandle);
    txTaskHandle = nullptr;
  }
  if (rxQueue) {
    vQueueDelete(rxQueue);
    rxQueue = nullptr;
  }
  if (txQueue) {
    spp_packet_t *pkt = nullptr;
    while (xQueueReceive(txQueue, &pkt, 0) == pdTRUE) {
      free(pkt);
    }
    vQueueDelete(txQueue);
    txQueue = nullptr;
  }
  if (txDone) {
    vSemaphoreDelete(txDone);
    txDone = nullptr;
  }
  if (sppEventGroup) {
    vEventGroupDelete(sppEventGroup);
    sppEventGroup = nullptr;
  }
  if (btEventGroup) {
    vEventGroupDelete(btEventGroup);
    btEventGroup = nullptr;
  }
}

void BluetoothSerial::Impl::txTask(void *arg) {
  auto *impl = static_cast<Impl *>(arg);
  spp_packet_t *pkt = nullptr;
  size_t len;

  for (;;) {
    if (!impl->sppEventGroup || !(xEventGroupGetBits(impl->sppEventGroup) & SPP_RUNNING)) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (xQueueReceive(impl->txQueue, &pkt, portMAX_DELAY) != pdTRUE || !pkt) {
      continue;
    }

    EventBits_t bits = xEventGroupWaitBits(impl->sppEventGroup, SPP_CONNECTED | SPP_CONGESTED, pdFALSE, pdFALSE, portMAX_DELAY);
    if (!(bits & SPP_CONNECTED)) {
      free(pkt);
      continue;
    }

    if (bits & SPP_CONGESTED) {
      xEventGroupWaitBits(impl->sppEventGroup, SPP_CONGESTED, pdTRUE, pdFALSE, portMAX_DELAY);
    }

    uint8_t *data = pkt->data;
    len = pkt->len;

    while (len > 0) {
      size_t chunk = (len > SPP_TX_MAX) ? SPP_TX_MAX : len;
      esp_err_t err = esp_spp_write(impl->sppClient, chunk, data);
      if (err != ESP_OK) {
        log_e("esp_spp_write: %s", esp_err_to_name(err));
        break;
      }
      xSemaphoreTake(impl->txDone, portMAX_DELAY);
      data += chunk;
      len -= chunk;
    }

    free(pkt);
  }
}

bool BluetoothSerial::Impl::getNameFromEIR(const uint8_t *eir, char *bdname, uint8_t *bdname_len) {
  if (!eir) return false;
  const uint8_t *rmt_bdname = esp_bt_gap_resolve_eir_data(const_cast<uint8_t *>(eir), ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, bdname_len);
  if (!rmt_bdname) {
    rmt_bdname = esp_bt_gap_resolve_eir_data(const_cast<uint8_t *>(eir), ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, bdname_len);
  }
  if (rmt_bdname) {
    if (*bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) *bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
    memcpy(bdname, rmt_bdname, *bdname_len);
    bdname[*bdname_len] = '\0';
    return true;
  }
  return false;
}

void BluetoothSerial::Impl::sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (!s_impl) return;
  auto *impl = s_impl;

  switch (event) {
    case ESP_SPP_INIT_EVT:
      log_i("SPP initialized");
      esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
      if (!impl->isInitiator) {
        esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, DEFAULT_SPP_NAME);
      }
      xEventGroupSetBits(impl->sppEventGroup, SPP_RUNNING);
      break;

    case ESP_SPP_SRV_OPEN_EVT:
    case ESP_SPP_OPEN_EVT:
      log_i("SPP connected, handle=%d", param->open.handle);
      if (event == ESP_SPP_OPEN_EVT) {
        impl->sppClient = param->open.handle;
      } else {
        impl->sppClient = param->srv_open.handle;
      }
      xEventGroupClearBits(impl->sppEventGroup, SPP_DISCONNECTED);
      xEventGroupSetBits(impl->sppEventGroup, SPP_CONNECTED);
      break;

    case ESP_SPP_CLOSE_EVT:
      log_i("SPP disconnected");
      impl->sppClient = 0;
      xEventGroupClearBits(impl->sppEventGroup, SPP_CONNECTED);
      xEventGroupSetBits(impl->sppEventGroup, SPP_DISCONNECTED);
      break;

    case ESP_SPP_DATA_IND_EVT:
      if (impl->dataCb) {
        impl->dataCb(param->data_ind.data, param->data_ind.len);
      } else if (impl->rxQueue) {
        for (uint16_t i = 0; i < param->data_ind.len; i++) {
          if (xQueueSend(impl->rxQueue, &param->data_ind.data[i], 0) != pdTRUE) {
            log_w("RX queue full, dropping byte");
            break;
          }
        }
      }
      break;

    case ESP_SPP_CONG_EVT:
      if (param->cong.cong) {
        xEventGroupSetBits(impl->sppEventGroup, SPP_CONGESTED);
      } else {
        xEventGroupClearBits(impl->sppEventGroup, SPP_CONGESTED);
      }
      break;

    case ESP_SPP_WRITE_EVT:
      if (impl->txDone) {
        xSemaphoreGive(impl->txDone);
      }
      break;

    case ESP_SPP_DISCOVERY_COMP_EVT:
      if (param->disc_comp.status == ESP_SPP_SUCCESS && impl->doConnect) {
        if (param->disc_comp.scn_num > 0) {
          esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_MASTER, param->disc_comp.scn[0], impl->peerAddr);
        }
        impl->doConnect = false;
      }
      break;

    default: break;
  }
}

void BluetoothSerial::Impl::gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  if (!s_impl) return;
  auto *impl = s_impl;

  switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
      BluetoothSerial::DiscoveryResult result;
      result.address = BTAddress(param->disc_res.bda, BTAddress::Type::Public);

      char bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = {};
      uint8_t bdname_len = 0;
      for (int i = 0; i < param->disc_res.num_prop; i++) {
        auto &prop = param->disc_res.prop[i];
        switch (prop.type) {
          case ESP_BT_GAP_DEV_PROP_BDNAME:
            if (prop.len <= ESP_BT_GAP_MAX_BDNAME_LEN) {
              memcpy(bdname, prop.val, prop.len);
              bdname[prop.len] = '\0';
            }
            break;
          case ESP_BT_GAP_DEV_PROP_COD:
            if (prop.len >= 4) result.cod = *reinterpret_cast<uint32_t *>(prop.val);
            break;
          case ESP_BT_GAP_DEV_PROP_RSSI:
            if (prop.len >= 1) result.rssi = *reinterpret_cast<int8_t *>(prop.val);
            break;
          case ESP_BT_GAP_DEV_PROP_EIR:
            getNameFromEIR(reinterpret_cast<uint8_t *>(prop.val), bdname, &bdname_len);
            break;
          default: break;
        }
      }
      result.name = String(bdname);

      if (impl->targetName.length() > 0 && result.name == impl->targetName) {
        memcpy(impl->peerAddr, param->disc_res.bda, 6);
        impl->isRemoteAddressSet = true;
        esp_bt_gap_cancel_discovery();
      }

      impl->discoveryResults.push_back(result);

      if (impl->discoveryCb) {
        impl->discoveryCb(result);
      }
      break;
    }

    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
      if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        xEventGroupSetBits(impl->btEventGroup, BT_DISCOVERY_COMPLETED);
        xEventGroupClearBits(impl->btEventGroup, BT_DISCOVERY_RUNNING);
      } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
        xEventGroupSetBits(impl->btEventGroup, BT_DISCOVERY_RUNNING);
        xEventGroupClearBits(impl->btEventGroup, BT_DISCOVERY_COMPLETED);
      }
      break;

    case ESP_BT_GAP_PIN_REQ_EVT:
      if (impl->pinCodeLen > 0) {
        esp_bt_gap_pin_reply(param->pin_req.bda, true, impl->pinCodeLen, impl->pinCode);
      }
      break;

    case ESP_BT_GAP_CFM_REQ_EVT:
      if (impl->confirmRequestCb) {
        impl->confirmRequestCb(param->cfm_req.num_val);
      } else {
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
      }
      break;

    case ESP_BT_GAP_AUTH_CMPL_EVT:
      if (impl->authCompleteCb) {
        impl->authCompleteCb(param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS);
      }
      break;

    default: break;
  }
}

// --------------------------------------------------------------------------
// BluetoothSerial public API
// --------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

BluetoothSerial::BluetoothSerial() : _impl(std::make_unique<Impl>()) {}

BluetoothSerial::~BluetoothSerial() {
  if (_impl && _impl->initialized) {
    end(false);
  }
}

BTStatus BluetoothSerial::begin(const String &localName, bool isInitiator) {
  if (!_impl) return BTStatus::InvalidState;
  if (_impl->initialized) return BTStatus::OK;

  if (s_impl) return BTStatus::Busy;

  _impl->localName = localName;
  _impl->isInitiator = isInitiator;

  if (!btStarted() && !btStart()) {
    log_e("btStart() failed");
    return BTStatus::Fail;
  }

  esp_err_t err = esp_bluedroid_init();
  if (err != ESP_OK) {
    log_e("esp_bluedroid_init: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  err = esp_bluedroid_enable();
  if (err != ESP_OK) {
    log_e("esp_bluedroid_enable: %s", esp_err_to_name(err));
    esp_bluedroid_deinit();
    return BTStatus::Fail;
  }

  err = esp_bt_gap_register_callback(Impl::gapCallback);
  if (err != ESP_OK) {
    log_e("esp_bt_gap_register_callback: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  err = esp_spp_register_callback(Impl::sppCallback);
  if (err != ESP_OK) {
    log_e("esp_spp_register_callback: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  esp_spp_cfg_t cfg = BT_SPP_DEFAULT_CONFIG();
  cfg.mode = ESP_SPP_MODE_CB;
  err = esp_spp_enhanced_init(&cfg);
  if (err != ESP_OK) {
    log_e("esp_spp_enhanced_init: %s", esp_err_to_name(err));
    return BTStatus::Fail;
  }

  if (localName.length() > 0) {
    esp_bt_gap_set_device_name(localName.c_str());
  }

  if (_impl->sspEnabled) {
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap;
    if (_impl->ioCapInput && _impl->ioCapOutput) iocap = ESP_BT_IO_CAP_IO;
    else if (_impl->ioCapInput) iocap = ESP_BT_IO_CAP_IN;
    else if (_impl->ioCapOutput) iocap = ESP_BT_IO_CAP_OUT;
    else iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(esp_bt_io_cap_t));
    esp_bt_gap_set_security_param(ESP_BT_SP_MIN_KEY_SIZE, nullptr, 0);
  } else if (_impl->pinCodeLen > 0) {
    esp_bt_pin_type_t pinType = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_gap_set_pin(pinType, _impl->pinCodeLen, _impl->pinCode);
  }

  if (!_impl->createResources()) {
    log_e("Failed to allocate BluetoothSerial resources");
    return BTStatus::NoMemory;
  }

  s_impl = _impl.get();
  _impl->initialized = true;

  EventBits_t bits = xEventGroupWaitBits(_impl->sppEventGroup, SPP_RUNNING, pdFALSE, pdTRUE, pdMS_TO_TICKS(5000));
  if (!(bits & SPP_RUNNING)) {
    log_e("SPP server failed to start within timeout");
    _impl->initialized = false;
    s_impl = nullptr;
    _impl->destroyResources();
    return BTStatus::Timeout;
  }

  return BTStatus::OK;
}

void BluetoothSerial::end(bool releaseMemory) {
  if (!_impl || !_impl->initialized) return;

  _impl->initialized = false;
  s_impl = nullptr;

  esp_spp_deinit();
  esp_bluedroid_disable();
  esp_bluedroid_deinit();

  btStop();

  if (releaseMemory) {
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  }

  _impl->destroyResources();
}

int BluetoothSerial::available() {
  if (!_impl || !_impl->rxQueue) return 0;
  return uxQueueMessagesWaiting(_impl->rxQueue);
}

int BluetoothSerial::peek() {
  if (!_impl || !_impl->rxQueue) return -1;
  uint8_t c;
  if (xQueuePeek(_impl->rxQueue, &c, 0) == pdTRUE) return c;
  return -1;
}

int BluetoothSerial::read() {
  if (!_impl || !_impl->rxQueue) return -1;
  uint8_t c;
  if (xQueueReceive(_impl->rxQueue, &c, 0) == pdTRUE) return c;
  return -1;
}

size_t BluetoothSerial::write(uint8_t c) {
  return write(&c, 1);
}

size_t BluetoothSerial::write(const uint8_t *buf, size_t len) {
  if (!_impl || !_impl->txQueue || !_impl->initialized || len == 0) return 0;

  auto *pkt = static_cast<spp_packet_t *>(malloc(sizeof(spp_packet_t) + len));
  if (!pkt) return 0;
  pkt->len = len;
  memcpy(pkt->data, buf, len);

  if (xQueueSend(_impl->txQueue, &pkt, pdMS_TO_TICKS(1000)) != pdTRUE) {
    free(pkt);
    return 0;
  }
  return len;
}

void BluetoothSerial::flush() {
  if (!_impl || !_impl->txQueue) return;
  while (uxQueueMessagesWaiting(_impl->txQueue) > 0) {
    delay(5);
  }
}

BTStatus BluetoothSerial::connect(const String &remoteName, uint32_t timeoutMs) {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;

  _impl->targetName = remoteName;
  _impl->isRemoteAddressSet = false;
  _impl->doConnect = true;

  xEventGroupClearBits(_impl->btEventGroup, BT_DISCOVERY_COMPLETED);
  esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, (timeoutMs / 1280) + 1, 0);

  EventBits_t bits = xEventGroupWaitBits(_impl->btEventGroup, BT_DISCOVERY_COMPLETED, pdTRUE, pdTRUE, pdMS_TO_TICKS(timeoutMs + 2000));

  if (!_impl->isRemoteAddressSet) {
    _impl->targetName = "";
    return BTStatus::Fail;
  }

  esp_spp_start_discovery(ESP_SPP_SEC_AUTHENTICATE, _impl->peerAddr);

  bits = xEventGroupWaitBits(_impl->sppEventGroup, SPP_CONNECTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeoutMs));
  if (!(bits & SPP_CONNECTED)) {
    return BTStatus::Timeout;
  }

  _impl->targetName = "";
  return BTStatus::OK;
}

BTStatus BluetoothSerial::connect(const BTAddress &address, uint8_t channel) {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;

  memcpy(_impl->peerAddr, address.data(), 6);
  _impl->isRemoteAddressSet = true;
  _impl->doConnect = true;

  if (channel > 0) {
    esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_MASTER, channel, _impl->peerAddr);
  } else {
    esp_spp_start_discovery(ESP_SPP_SEC_AUTHENTICATE, _impl->peerAddr);
  }

  EventBits_t bits = xEventGroupWaitBits(_impl->sppEventGroup, SPP_CONNECTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
  if (!(bits & SPP_CONNECTED)) {
    return BTStatus::Timeout;
  }
  return BTStatus::OK;
}

BTStatus BluetoothSerial::disconnect() {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;
  if (_impl->sppClient == 0) return BTStatus::OK;
  esp_spp_disconnect(_impl->sppClient);
  xEventGroupWaitBits(_impl->sppEventGroup, SPP_DISCONNECTED, pdTRUE, pdTRUE, pdMS_TO_TICKS(5000));
  return BTStatus::OK;
}

bool BluetoothSerial::connected() const {
  if (!_impl || !_impl->sppEventGroup) return false;
  return (xEventGroupGetBits(_impl->sppEventGroup) & SPP_CONNECTED) != 0;
}

bool BluetoothSerial::hasClient() const {
  return connected();
}

std::vector<BluetoothSerial::DiscoveryResult> BluetoothSerial::discover(uint32_t timeoutMs) {
  if (!_impl || !_impl->initialized) return {};

  _impl->discoveryResults.clear();
  _impl->targetName = "";

  xEventGroupClearBits(_impl->btEventGroup, BT_DISCOVERY_COMPLETED);
  esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, (timeoutMs / 1280) + 1, 0);
  xEventGroupWaitBits(_impl->btEventGroup, BT_DISCOVERY_COMPLETED, pdTRUE, pdTRUE, pdMS_TO_TICKS(timeoutMs + 2000));

  return _impl->discoveryResults;
}

BTStatus BluetoothSerial::discoverAsync(std::function<void(const DiscoveryResult &)> callback, uint32_t timeoutMs) {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;

  _impl->discoveryCb = std::move(callback);
  _impl->discoveryResults.clear();
  _impl->targetName = "";

  xEventGroupClearBits(_impl->btEventGroup, BT_DISCOVERY_COMPLETED);
  esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, (timeoutMs / 1280) + 1, 0);
  return (err == ESP_OK) ? BTStatus::OK : BTStatus::Fail;
}

BTStatus BluetoothSerial::discoverStop() {
  if (!_impl || !_impl->initialized) return BTStatus::NotInitialized;
  esp_err_t err = esp_bt_gap_cancel_discovery();
  _impl->discoveryCb = nullptr;
  return (err == ESP_OK) ? BTStatus::OK : BTStatus::Fail;
}

void BluetoothSerial::enableSSP(bool input, bool output) {
  if (!_impl) return;
  _impl->sspEnabled = true;
  _impl->ioCapInput = input;
  _impl->ioCapOutput = output;
}

void BluetoothSerial::disableSSP() {
  if (_impl) _impl->sspEnabled = false;
}

void BluetoothSerial::setPin(const char *pin) {
  if (!_impl || !pin) return;
  size_t len = strlen(pin);
  if (len > 16) len = 16;
  memcpy(_impl->pinCode, pin, len);
  _impl->pinCodeLen = len;
}

BTStatus BluetoothSerial::onConfirmRequest(std::function<void(uint32_t)> callback) {
  if (!_impl) return BTStatus::InvalidState;
  _impl->confirmRequestCb = std::move(callback);
  return BTStatus::OK;
}

BTStatus BluetoothSerial::onAuthComplete(std::function<void(bool)> callback) {
  if (!_impl) return BTStatus::InvalidState;
  _impl->authCompleteCb = std::move(callback);
  return BTStatus::OK;
}

void BluetoothSerial::confirmReply(bool accept) {
  if (!_impl || !_impl->initialized) return;
  esp_bd_addr_t bda;
  memcpy(bda, _impl->peerAddr, 6);
  esp_bt_gap_ssp_confirm_reply(bda, accept);
}

std::vector<BTAddress> BluetoothSerial::getBondedDevices() {
  std::vector<BTAddress> result;
  int count = esp_bt_gap_get_bond_device_num();
  if (count <= 0) return result;

  auto *list = static_cast<esp_bd_addr_t *>(malloc(count * sizeof(esp_bd_addr_t)));
  if (!list) return result;

  esp_err_t err = esp_bt_gap_get_bond_device_list(&count, list);
  if (err == ESP_OK) {
    for (int i = 0; i < count; i++) {
      result.push_back(BTAddress(list[i], BTAddress::Type::Public));
    }
  }
  free(list);
  return result;
}

BTStatus BluetoothSerial::deleteBond(const BTAddress &address) {
  esp_bd_addr_t bda;
  memcpy(bda, address.data(), 6);
  esp_err_t err = esp_bt_gap_remove_bond_device(bda);
  return (err == ESP_OK) ? BTStatus::OK : BTStatus::Fail;
}

BTStatus BluetoothSerial::deleteAllBonds() {
  auto devices = getBondedDevices();
  for (const auto &addr : devices) {
    BTStatus s = deleteBond(addr);
    if (!s) return s;
  }
  return BTStatus::OK;
}

BTStatus BluetoothSerial::onData(std::function<void(const uint8_t *, size_t)> callback) {
  if (!_impl) return BTStatus::InvalidState;
  _impl->dataCb = std::move(callback);
  return BTStatus::OK;
}

BTAddress BluetoothSerial::getAddress() const {
  if (!_impl || !_impl->initialized) return BTAddress();
  const uint8_t *addr = esp_bt_dev_get_address();
  if (!addr) return BTAddress();
  return BTAddress(addr, BTAddress::Type::Public);
}

BluetoothSerial::operator bool() const {
  return _impl && _impl->initialized;
}

#pragma GCC diagnostic pop

#endif /* SOC_BT_SUPPORTED && CONFIG_BT_ENABLED && CONFIG_BLUEDROID_ENABLED */
