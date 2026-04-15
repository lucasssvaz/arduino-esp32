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

#include "BLESync.h"
#include "esp32-hal-log.h"

BTStatus BLESync::wait(uint32_t timeoutMs) {
  if (_waiter == nullptr) {
    log_e("BLESync::wait called without take()");
    return BTStatus::InvalidState;
  }

  TickType_t ticks = (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);

  uint32_t notified = ulTaskNotifyTake(pdTRUE, ticks);
  if (notified == 0) {
    _waiter = nullptr;
    log_w("BLESync::wait timed out after %u ms", timeoutMs);
    return BTStatus::Timeout;
  }

  _waiter = nullptr;
  return _status;
}

#endif /* BLE_ENABLED */
