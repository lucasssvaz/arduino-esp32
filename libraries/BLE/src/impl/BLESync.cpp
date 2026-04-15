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

void BLESync::take() {
  _status = BTStatus::OK;
  if (!_sem) {
    _sem = xSemaphoreCreateBinary();
  }
  // Drain any leftover give() from a timed-out or cancelled operation
  xSemaphoreTake(_sem, 0);
}

BTStatus BLESync::wait(uint32_t timeoutMs) {
  if (!_sem) {
    log_e("BLESync::wait called without take()");
    return BTStatus::InvalidState;
  }

  TickType_t ticks = (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);

  if (xSemaphoreTake(_sem, ticks) != pdTRUE) {
    log_w("BLESync::wait timed out after %u ms", timeoutMs);
    return BTStatus::Timeout;
  }

  return _status;
}

void BLESync::give(BTStatus status) {
  _status = status;
  if (_sem) {
    xSemaphoreGive(_sem);
  }
}

#endif /* BLE_ENABLED */
