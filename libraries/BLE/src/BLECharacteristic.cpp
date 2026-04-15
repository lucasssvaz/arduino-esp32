/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
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
#if BLE_ENABLED

#include "impl/BLECharacteristicBackend.h"
#include "impl/BLEImplHelpers.h"

// --------------------------------------------------------------------------
// BLECharacteristic common API (stack-agnostic)
// --------------------------------------------------------------------------

BLECharacteristic::BLECharacteristic() : _impl(nullptr) {}
BLECharacteristic::operator bool() const { return _impl != nullptr; }

void BLECharacteristic::setValue(const String &value) {
  setValue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
}
void BLECharacteristic::setValue(uint16_t v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }
void BLECharacteristic::setValue(uint32_t v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }
void BLECharacteristic::setValue(float v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }
void BLECharacteristic::setValue(double v) { setValue(reinterpret_cast<const uint8_t *>(&v), sizeof(v)); }

BLEProperty BLECharacteristic::getProperties() const {
  return _impl ? _impl->properties : BLEProperty{};
}

BLEPermission BLECharacteristic::getPermissions() const {
  return _impl ? _impl->permissions : BLEPermission{};
}

void BLECharacteristic::setPermissions(BLEPermission permissions) {
  BLE_CHECK_IMPL();
  impl.permissions = permissions;
}

BLEDescriptor BLECharacteristic::getDescriptor(const BLEUUID &uuid) {
  BLE_CHECK_IMPL(BLEDescriptor());
  for (auto &d : impl.descriptors) {
    if (d->uuid == uuid) {
      return BLEDescriptor(d);
    }
  }
  return BLEDescriptor();
}

std::vector<BLEDescriptor> BLECharacteristic::getDescriptors() const {
  std::vector<BLEDescriptor> result;
  BLE_CHECK_IMPL(result);
  result.reserve(impl.descriptors.size());
  for (auto &d : impl.descriptors) {
    result.push_back(BLEDescriptor(d));
  }
  return result;
}

void BLECharacteristic::removeDescriptor(const BLEDescriptor &desc) {
  if (!_impl || !desc._impl) return;
  auto &descs = _impl->descriptors;
  for (auto it = descs.begin(); it != descs.end(); ++it) {
    if (*it == desc._impl) {
      descs.erase(it);
      break;
    }
  }
}

BLEUUID BLECharacteristic::getUUID() const {
  return _impl ? _impl->uuid : BLEUUID();
}

uint16_t BLECharacteristic::getHandle() const {
  return _impl ? _impl->handle : 0;
}

BLEService BLECharacteristic::getService() const {
  return _impl && _impl->service ? BLEService(std::shared_ptr<BLEService::Impl>(_impl->service, [](BLEService::Impl *){})) : BLEService();
}

String BLECharacteristic::toString() const {
  BLE_CHECK_IMPL("BLECharacteristic(null)");
  return "BLECharacteristic(uuid=" + impl.uuid.toString() + ")";
}

#endif /* BLE_ENABLED */
