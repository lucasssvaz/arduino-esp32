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

#include "impl/BLEGuards.h"
#if BLE_ENABLED

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "BTStatus.h"

/**
 * @brief Internal synchronization primitive for BLE operations.
 *
 * Each instance owns a private binary semaphore so that multiple BLESync
 * objects waiting on the same task cannot steal each other's notifications
 * (which was a fatal flaw of the earlier task-notification design).
 *
 * Usage pattern (blocking operation):
 *   1. Arduino task calls take() then starts the async BLE operation.
 *   2. Arduino task calls wait(timeoutMs) to block.
 *   3. BLE stack callback calls give(status) to unblock the waiter.
 *   4. Arduino task reads the returned BTStatus.
 *
 * @note This is an internal class. Not part of the public API.
 */
class BLESync {
public:
  BLESync() : _sem(nullptr), _status(BTStatus::OK) {}

  ~BLESync() {
    if (_sem) vSemaphoreDelete(_sem);
  }

  BLESync(const BLESync &) = delete;
  BLESync &operator=(const BLESync &) = delete;

  /**
   * @brief Prepare for a blocking wait.
   * Lazily creates the semaphore and drains any prior give().
   * Must be called from the task that will subsequently call wait().
   */
  void take();

  /**
   * @brief Block the calling task until give() is called or timeout expires.
   * @param timeoutMs Timeout in milliseconds. Use portMAX_DELAY for infinite wait.
   * @return BTStatus from the give() call, or BTStatus::Timeout if timed out.
   */
  BTStatus wait(uint32_t timeoutMs = portMAX_DELAY);

  /**
   * @brief Unblock the waiting task. Typically called from a BLE stack callback.
   * @param status Status to propagate to the waiter.
   */
  void give(BTStatus status = BTStatus::OK);

  /** @brief Get the last status set by give(). */
  BTStatus status() const {
    return _status;
  }

private:
  SemaphoreHandle_t _sem;
  BTStatus _status;
};

#endif /* BLE_ENABLED */
