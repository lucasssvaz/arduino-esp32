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

#include "impl/BLEGuards.h"
#if BLE_NIMBLE

#include "NimBLERemoteTypes.h"
#include "NimBLEUUID.h"
#include "esp32-hal-log.h"
#include "impl/BLEImplHelpers.h"

// ==========================================================================
// BLERemoteService implementation
// ==========================================================================

BLEClient BLERemoteService::getClient() const {
  return _impl && _impl->client ? BLEClient(std::shared_ptr<BLEClient::Impl>(_impl->client, [](BLEClient::Impl *){})) : BLEClient();
}

BLERemoteCharacteristic BLERemoteService::getCharacteristic(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLERemoteCharacteristic());

  if (!impl.charsDiscovered) {
    if (!isGattConnected(impl.connHandle)) return BLERemoteCharacteristic();

    impl.chrDiscoverSync.take();
    int rc = ble_gattc_disc_all_chrs(impl.connHandle, impl.startHandle, impl.endHandle,
                                     Impl::chrDiscoveryCb, _impl.get());
    if (rc != 0) {
      impl.chrDiscoverSync.give(BTStatus::Fail);
      return BLERemoteCharacteristic();
    }
    BTStatus status = impl.chrDiscoverSync.wait(10000);
    if (status == BTStatus::OK) {
      impl.charsDiscovered = true;
      for (auto &c : impl.characteristics) {
        c->connHandle = impl.connHandle;
        c->service = _impl.get();
      }
    }
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
  BLE_CHECK_IMPL(result);
  for (auto &c : impl.characteristics) {
    result.push_back(BLERemoteCharacteristic(c));
  }
  return result;
}

int BLERemoteService::Impl::chrDiscoveryCb(uint16_t connHandle, const struct ble_gatt_error *error,
                                            const struct ble_gatt_chr *chr, void *arg) {
  auto *impl = static_cast<BLERemoteService::Impl *>(arg);
  if (!impl) return 0;

  if (error->status == 0 && chr != nullptr) {
    auto cImpl = std::make_shared<BLERemoteCharacteristic::Impl>();
    cImpl->uuid = nimbleToUuid(chr->uuid);
    cImpl->defHandle = chr->def_handle;
    cImpl->valHandle = chr->val_handle;
    cImpl->properties = chr->properties;
    impl->characteristics.push_back(cImpl);
    return 0;
  }

  if (error->status == BLE_HS_EDONE) {
    impl->chrDiscoverSync.give(BTStatus::OK);
  } else {
    impl->chrDiscoverSync.give(BTStatus::Fail);
  }
  return 0;
}

#endif /* BLE_NIMBLE */
