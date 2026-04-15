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

/*
 * Bluedroid does not support L2CAP CoC.
 * BLE_L2CAP_SUPPORTED is always 0 when BLE_BLUEDROID is selected, so this
 * file intentionally compiles to nothing.  It exists only so that the
 * source tree mirrors the NimBLE backend and a future Bluedroid
 * implementation has a clear place to land.
 */
