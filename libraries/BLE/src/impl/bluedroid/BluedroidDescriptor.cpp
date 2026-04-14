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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if defined(SOC_BLE_SUPPORTED) && defined(CONFIG_BLUEDROID_ENABLED)

#include "BluedroidDescriptor.h"
#include "impl/BLEImplHelpers.h"

// --------------------------------------------------------------------------
// BLEDescriptor -- Bluedroid
// --------------------------------------------------------------------------

BLEDescriptor::BLEDescriptor(const BLEUUID &uuid, uint16_t) : _impl(nullptr) {
  _impl = std::make_shared<BLEDescriptor::Impl>();
  _impl->uuid = uuid;
}

void BLEDescriptor::setValue(const uint8_t *data, size_t length) {
  BLE_CHECK_IMPL(); impl.value.assign(data, data + length);
}

const uint8_t *BLEDescriptor::getValue(size_t *length) const {
  if (!_impl || _impl->value.empty()) { if (length) *length = 0; return nullptr; }
  if (length) *length = _impl->value.size();
  return _impl->value.data();
}

void BLEDescriptor::setPermissions(BLEPermission) {}

BTStatus BLEDescriptor::onRead(ReadHandler) { return BTStatus::NotSupported; }
BTStatus BLEDescriptor::onWrite(WriteHandler) { return BTStatus::NotSupported; }

BLEDescriptor BLEDescriptor::createUserDescription(const String &text) {
  auto impl = std::make_shared<BLEDescriptor::Impl>();
  impl->uuid = BLEUUID(static_cast<uint16_t>(0x2901));
  impl->value.assign(text.c_str(), text.c_str() + text.length());
  return BLEDescriptor(impl);
}

BLEDescriptor BLEDescriptor::createPresentationFormat() {
  auto impl = std::make_shared<BLEDescriptor::Impl>();
  impl->uuid = BLEUUID(static_cast<uint16_t>(0x2904));
  impl->value.resize(7, 0);
  return BLEDescriptor(impl);
}

BLEDescriptor BLEDescriptor::createCCCD() {
  auto impl = std::make_shared<BLEDescriptor::Impl>();
  impl->uuid = BLEUUID(static_cast<uint16_t>(0x2902));
  impl->value.resize(2, 0);
  return BLEDescriptor(impl);
}

#endif /* SOC_BLE_SUPPORTED && CONFIG_BLUEDROID_ENABLED */
