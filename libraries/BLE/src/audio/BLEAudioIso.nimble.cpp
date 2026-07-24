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

/**
 * @file BLEAudioIso.nimble.cpp
 * @brief NimBLE host-event -> ISO engine forwarding (see BLEAudioIso.nimble.h).
 */

#include "core/BLEGuards.h"

#if BLE_NIMBLE && BLE_ISO_SUPPORTED

#include "audio/BLEAudioIso.nimble.h"
#include "audio/BLEAudioIso.h"
#include "audio/BLEAudioIsoVendor.h"

#include <host/ble_gap.h>

namespace BLEAudioIso {

void forwardHostGapEvent(struct ble_gap_event *event) {
  if (event == nullptr || !isActive()) {
    return;
  }

  switch (event->type) {
    // Periodic-advertising sync + BIGInfo events feed the BIS receiver: the
    // engine emits its BIGInfo report (which arms syncBig) from these.
    case BLE_GAP_EVENT_PERIODIC_SYNC:
    case BLE_GAP_EVENT_PERIODIC_REPORT:
    case BLE_GAP_EVENT_PERIODIC_SYNC_LOST:
#ifdef BLE_GAP_EVENT_PERIODIC_TRANSFER
    case BLE_GAP_EVENT_PERIODIC_TRANSFER:
#endif
#ifdef BLE_GAP_EVENT_PERIODIC_TRANSFER_V2
    case BLE_GAP_EVENT_PERIODIC_TRANSFER_V2:
#endif
#ifdef BLE_GAP_EVENT_BIGINFO_REPORT
    case BLE_GAP_EVENT_BIGINFO_REPORT:
#endif
      bleIsoVendorGapPostEvent((uint8_t)event->type, event);
      break;
    default: break;
  }
}

}  // namespace BLEAudioIso

#endif /* BLE_NIMBLE && BLE_ISO_SUPPORTED */
