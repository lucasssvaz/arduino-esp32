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

#include "impl/BLEGuards.h"
#if BLE_ENABLED

#include "BLEAdvTypes.h"

bool BLEConnParams::isValid() const {
  if (minInterval < 6 || maxInterval > 3200 || minInterval > maxInterval) {
    return false;
  }
  if (latency > 499) {
    return false;
  }
  if (timeout < 10 || timeout > 3200) {
    return false;
  }
  // Supervision_Timeout (10ms units) > (1 + latency) * maxInterval (1.25ms units) * 2
  // Multiply both sides by 10 to avoid float:
  // timeout * 100 > (1 + latency) * maxInterval * 25
  if ((uint32_t)timeout * 100 <= (uint32_t)(1 + latency) * maxInterval * 25) {
    return false;
  }
  return true;
}

#endif /* BLE_ENABLED */
