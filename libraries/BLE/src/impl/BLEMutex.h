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
 * @brief Lightweight mutex wrapping a FreeRTOS recursive mutex.
 *
 * Drop-in replacement for BLEMutex that avoids pulling in the heavy
 * <mutex> header (which transitively includes <string>, <functional>,
 * <system_error>, and locale/IO machinery -- adding ~20 KB to flash).
 *
 * @note Uses a recursive mutex so the same task can lock multiple times
 *       without deadlocking -- matches the behavior of NimBLE callbacks
 *       that may re-enter through the same task context.
 */
class BLEMutex {
public:
  BLEMutex() : _handle(xSemaphoreCreateRecursiveMutex()) {}

  ~BLEMutex() {
    if (_handle) {
      vSemaphoreDelete(_handle);
    }
  }

  BLEMutex(const BLEMutex &) = delete;
  BLEMutex &operator=(const BLEMutex &) = delete;

  void lock() {
    if (_handle) {
      xSemaphoreTakeRecursive(_handle, portMAX_DELAY);
    }
  }

  void unlock() {
    if (_handle) {
      xSemaphoreGiveRecursive(_handle);
    }
  }

private:
  SemaphoreHandle_t _handle;
};

/**
 * @brief RAII lock guard for BLEMutex.  Drop-in for BLELockGuard.
 */
class BLELockGuard {
public:
  explicit BLELockGuard(BLEMutex &m) : _mtx(m) { _mtx.lock(); }
  ~BLELockGuard() { _mtx.unlock(); }

  BLELockGuard(const BLELockGuard &) = delete;
  BLELockGuard &operator=(const BLELockGuard &) = delete;

private:
  BLEMutex &_mtx;
};
