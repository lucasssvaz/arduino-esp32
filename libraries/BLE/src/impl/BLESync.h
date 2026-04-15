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
#include "freertos/task.h"
#include "BTStatus.h"

/**
 * @brief Internal synchronization primitive for BLE operations.
 *
 * Uses FreeRTOS task notifications for ~45% faster performance and
 * zero additional RAM compared to binary semaphores.
 *
 * Usage pattern (blocking operation):
 *   1. Arduino task calls take() then starts the async BLE operation.
 *   2. Arduino task calls wait(timeoutMs) to block.
 *   3. BLE stack callback calls give(status) to unblock the waiter.
 *   4. Arduino task reads the returned BTStatus.
 *
 * @warning Task notifications are 1:1 -- only one task can wait at a time.
 * @note This is an internal class. Not part of the public API.
 */
class BLESync {
public:
  BLESync() : _waiter(nullptr), _status(BTStatus::OK) {}

  ~BLESync() = default;

  BLESync(const BLESync &) = delete;
  BLESync &operator=(const BLESync &) = delete;

  /**
   * @brief Prepare for a blocking wait. Records the calling task as the waiter.
   * Must be called from the task that will subsequently call wait().
   */
  void take() {
    _status = BTStatus::OK;
    _waiter = xTaskGetCurrentTaskHandle();
  }

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
  void give(BTStatus status = BTStatus::OK) {
    _status = status;
    if (_waiter != nullptr) {
      xTaskNotifyGive(_waiter);
    }
  }

  /** @brief Get the last status set by give(). */
  BTStatus status() const {
    return _status;
  }

private:
  TaskHandle_t _waiter;
  BTStatus _status;
};

#endif /* BLE_ENABLED */
