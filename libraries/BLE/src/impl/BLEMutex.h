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

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief RAII lock guard for a FreeRTOS recursive mutex (SemaphoreHandle_t).
 *
 * Use in place of std::lock_guard to avoid the ~20 KB flash overhead
 * that comes from including <mutex>.
 */
class BLELockGuard {
public:
  explicit BLELockGuard(SemaphoreHandle_t m) : _mtx(m) {
    if (_mtx) {
      xSemaphoreTakeRecursive(_mtx, portMAX_DELAY);
    }
  }

  ~BLELockGuard() {
    if (_mtx) {
      xSemaphoreGiveRecursive(_mtx);
    }
  }

  BLELockGuard(const BLELockGuard &) = delete;
  BLELockGuard &operator=(const BLELockGuard &) = delete;

private:
  SemaphoreHandle_t _mtx;
};
