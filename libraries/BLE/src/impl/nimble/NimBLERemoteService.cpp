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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if (defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)) && defined(CONFIG_NIMBLE_ENABLED)

#include "NimBLERemoteTypes.h"
#include "esp32-hal-log.h"
#include "impl/BLEImplHelpers.h"

// --------------------------------------------------------------------------
// UUID conversion helpers (NimBLE ble_uuid_any_t <-> BLEUUID)
// --------------------------------------------------------------------------

BLEUUID nimbleUuidToPublic(const ble_uuid_any_t &u) {
  if (u.u.type == BLE_UUID_TYPE_16) {
    return BLEUUID(static_cast<uint16_t>(u.u16.value));
  } else if (u.u.type == BLE_UUID_TYPE_32) {
    return BLEUUID(static_cast<uint32_t>(u.u32.value));
  } else {
    return BLEUUID(u.u128.value, 16, true);
  }
}

void publicUuidToNimble(const BLEUUID &uuid, ble_uuid_any_t &out) {
  memset(&out, 0, sizeof(out));
  if (uuid.bitSize() == 16) {
    out.u.type = BLE_UUID_TYPE_16;
    out.u16.value = uuid.toUint16();
  } else if (uuid.bitSize() == 32) {
    out.u.type = BLE_UUID_TYPE_32;
    out.u32.value = uuid.toUint32();
  } else {
    out.u.type = BLE_UUID_TYPE_128;
    BLEUUID full = uuid.to128();
    const uint8_t *be = full.data();
    for (int i = 0; i < 16; i++) {
      out.u128.value[i] = be[15 - i];
    }
  }
}

// ==========================================================================
// BLERemoteService implementation
// ==========================================================================

BLERemoteService::BLERemoteService() : _impl(nullptr) {}
BLERemoteService::operator bool() const { return _impl != nullptr; }

BLEUUID BLERemoteService::getUUID() const {
  return _impl ? _impl->uuid : BLEUUID();
}

uint16_t BLERemoteService::getHandle() const {
  return _impl ? _impl->startHandle : 0;
}

BLEClient BLERemoteService::getClient() const {
  return _impl && _impl->clientImpl ? BLEClient(std::shared_ptr<BLEClient::Impl>(_impl->clientImpl, [](BLEClient::Impl *){})) : BLEClient();
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
        c->serviceImpl = _impl.get();
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

String BLERemoteService::getValue(const BLEUUID &charUUID) {
  BLERemoteCharacteristic chr = getCharacteristic(charUUID);
  if (!chr) return "";
  return chr.readValue();
}

BTStatus BLERemoteService::setValue(const BLEUUID &charUUID, const String &value) {
  BLERemoteCharacteristic chr = getCharacteristic(charUUID);
  if (!chr) return BTStatus::NotFound;
  return chr.writeValue(value);
}

String BLERemoteService::toString() const {
  BLE_CHECK_IMPL("BLERemoteService(empty)");
  return "BLERemoteService(uuid=" + impl.uuid.toString() + ")";
}

int BLERemoteService::Impl::chrDiscoveryCb(uint16_t connHandle, const struct ble_gatt_error *error,
                                            const struct ble_gatt_chr *chr, void *arg) {
  auto *impl = static_cast<BLERemoteService::Impl *>(arg);
  if (!impl) return 0;

  if (error->status == 0 && chr != nullptr) {
    auto cImpl = std::make_shared<BLERemoteCharacteristic::Impl>();
    cImpl->uuid = nimbleUuidToPublic(chr->uuid);
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

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
