/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2017 Neil Kolban
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

#include "impl/BLEGuards.h"
#if BLE_BLUEDROID

#include "BLE.h"
#include "BluedroidClient.h"
#include "BluedroidRemoteTypes.h"
#include "BluedroidUUID.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <esp_gattc_api.h>

// --------------------------------------------------------------------------
// BLERemoteService -- Bluedroid backend
// --------------------------------------------------------------------------

BLEClient BLERemoteService::getClient() const {
  return (_impl && _impl->client)
    ? BLEClient::Impl::makeHandle(_impl->client)
    : BLEClient();
}

BLERemoteCharacteristic BLERemoteService::getCharacteristic(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLERemoteCharacteristic());

  // Discover characteristics if not yet done
  if (!impl.characteristicsRetrieved && impl.client && impl.client->connected) {
    uint16_t count = 0;
    esp_gatt_status_t st = esp_ble_gattc_get_attr_count(
      impl.client->gattcIf, impl.client->connId,
      ESP_GATT_DB_CHARACTERISTIC,
      impl.startHandle, impl.endHandle, 0, &count);

    if (st == ESP_GATT_OK && count > 0) {
      std::vector<esp_gattc_char_elem_t> chars(count);
      st = esp_ble_gattc_get_all_char(
        impl.client->gattcIf, impl.client->connId,
        impl.startHandle, impl.endHandle,
        chars.data(), &count, 0);

      if (st == ESP_GATT_OK) {
        for (uint16_t i = 0; i < count; i++) {
          auto cImpl = std::make_shared<BLERemoteCharacteristic::Impl>();
          cImpl->uuid = espToUuid(chars[i].uuid);
          cImpl->handle = chars[i].char_handle;
          cImpl->defHandle = chars[i].char_handle;
          cImpl->properties = chars[i].properties;
          cImpl->service = _impl.get();
          impl.characteristics.push_back(cImpl);
        }
      }
    }
    impl.characteristicsRetrieved = true;
  }

  BLEUUID target = uuid.to128();
  for (auto &c : impl.characteristics) {
    if (c->uuid.to128() == target) {
      return BLERemoteCharacteristic(c);
    }
  }
  return BLERemoteCharacteristic();
}

std::vector<BLERemoteCharacteristic> BLERemoteService::getCharacteristics() const {
  std::vector<BLERemoteCharacteristic> result;
  if (!_impl) return result;

  // Trigger discovery through non-const cast (discovery is a lazy-init pattern)
  if (!_impl->characteristicsRetrieved && _impl->client && _impl->client->connected) {
    auto *self = const_cast<BLERemoteService *>(this);
    self->getCharacteristic(BLEUUID(static_cast<uint16_t>(0)));  // triggers discovery
  }

  for (auto &c : _impl->characteristics) {
    result.push_back(BLERemoteCharacteristic(c));
  }
  return result;
}

#endif /* BLE_BLUEDROID */
