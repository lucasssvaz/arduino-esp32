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
 * @file BLEAudioEngine.nimble.cpp
 * @brief NimBLE host-event -> LE Audio engine forwarding (see BLEAudioEngine.nimble.h).
 */

#include "core/BLEGuards.h"

#if BLE_NIMBLE && BLE_AUDIO_SUPPORTED

#include "audio/BLEAudioEngine.nimble.h"
#include "audio/BLEAudioEngine.h"
#include "audio/BLEAudioVendor.h"

#include <host/ble_gap.h>

namespace BLEAudioEngine {

void forwardHostGapEvent(struct ble_gap_event *event) {
  if (event == nullptr || !isInitialized()) {
    return;
  }

  switch (event->type) {
    // Connection/security events belong to the engine's GAP path.
    case BLE_GAP_EVENT_CONNECT:
    case BLE_GAP_EVENT_DISCONNECT:
    case BLE_GAP_EVENT_ENC_CHANGE:
      bleAudioVendorGapPostEvent((uint8_t)event->type, event);
      break;

    // Attribute-layer events belong to the engine's GATT path. Once the MTU is
    // exchanged the client-side profile discovery can be kicked (idempotent).
    case BLE_GAP_EVENT_MTU:
      bleAudioVendorGattPostEvent((uint8_t)event->type, event);
      forwardGattcDiscStart(event->mtu.conn_handle);
      break;
    case BLE_GAP_EVENT_NOTIFY_RX:
    case BLE_GAP_EVENT_NOTIFY_TX:
    case BLE_GAP_EVENT_SUBSCRIBE:
      bleAudioVendorGattPostEvent((uint8_t)event->type, event);
      break;

#if BLE5_SUPPORTED
    // Broadcast-sink path: the engine decodes BASE + BIGInfo from the periodic
    // train and drives PA_SYNC / PA_SYNC_LOST for the broadcast sink. Extended
    // scan reports let the engine's assistant/announcement handling see sources.
    case BLE_GAP_EVENT_EXT_DISC:
    case BLE_GAP_EVENT_PERIODIC_SYNC:
    case BLE_GAP_EVENT_PERIODIC_REPORT:
    case BLE_GAP_EVENT_PERIODIC_SYNC_LOST:
#ifdef BLE_GAP_EVENT_PERIODIC_TRANSFER
    case BLE_GAP_EVENT_PERIODIC_TRANSFER:
#endif
#ifdef BLE_GAP_EVENT_PERIODIC_TRANSFER_V2
    case BLE_GAP_EVENT_PERIODIC_TRANSFER_V2:
#endif
      bleAudioVendorGapPostEvent((uint8_t)event->type, event);
      break;
#endif

    default:
      break;
  }
}

}  // namespace BLEAudioEngine

#endif /* BLE_NIMBLE && BLE_AUDIO_SUPPORTED */
