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

#include "NimBLECharacteristic.h"
#include "impl/BLEImplHelpers.h"

// --------------------------------------------------------------------------
// BLEDescriptor -- NimBLE
// --------------------------------------------------------------------------

void BLEDescriptor::setPermissions(BLEPermission perms) {
  BLE_CHECK_IMPL();
  uint8_t flags = 0;
  uint16_t p = static_cast<uint16_t>(perms);
  if (p & 0x000F) flags |= BLE_ATT_F_READ;
  if (p & 0x00F0) flags |= BLE_ATT_F_WRITE;
  if (p & static_cast<uint16_t>(BLEPermission::ReadEncrypted)) flags |= BLE_ATT_F_READ_ENC;
  if (p & static_cast<uint16_t>(BLEPermission::ReadAuthenticated)) flags |= BLE_ATT_F_READ_AUTHEN;
  if (p & static_cast<uint16_t>(BLEPermission::ReadAuthorized)) flags |= BLE_ATT_F_READ_AUTHOR;
  if (p & static_cast<uint16_t>(BLEPermission::WriteEncrypted)) flags |= BLE_ATT_F_WRITE_ENC;
  if (p & static_cast<uint16_t>(BLEPermission::WriteAuthenticated)) flags |= BLE_ATT_F_WRITE_AUTHEN;
  if (p & static_cast<uint16_t>(BLEPermission::WriteAuthorized)) flags |= BLE_ATT_F_WRITE_AUTHOR;
  impl.attFlags = flags;
}

#endif /* BLE_NIMBLE */
