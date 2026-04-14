/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
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

#include "soc/soc_caps.h"
#include "sdkconfig.h"
#if (defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)) && defined(CONFIG_NIMBLE_ENABLED)

#include "BLE.h"

#include "NimBLEScan.h"
#include "NimBLEAdvertisedDevice.h"
#include "impl/BLEImplHelpers.h"
#include "esp32-hal-log.h"

#include <host/ble_hs.h>
#include <host/ble_gap.h>

namespace {

void dispatchResult(BLEScan::Impl *impl, BLEAdvertisedDevice device) {
  if (impl->callbacks) {
    impl->callbacks->onResult(device);
  }
  if (impl->onResultCb) impl->onResultCb(device);
}

void appendOrReplaceResult(BLEScan::Impl *impl, const BLEAdvertisedDevice &device) {
  impl->results.appendOrReplace(device);
}

void dispatchComplete(BLEScan::Impl *impl) {
  if (impl->callbacks) {
    impl->callbacks->onComplete(impl->results);
  }
  if (impl->onCompleteCb) impl->onCompleteCb(impl->results);
}

#if CONFIG_BT_NIMBLE_EXT_ADV
void dispatchPeriodicSync(BLEScan::Impl *impl, uint16_t syncHandle, uint8_t sid, const BTAddress &addr, BLEPhy phy,
                          uint16_t interval) {
  if (impl->callbacks) {
    impl->callbacks->onPeriodicSync(syncHandle, sid, addr, phy, interval);
  }
  if (impl->periodicSyncCb) impl->periodicSyncCb(syncHandle, sid, addr, phy, interval);
}

void dispatchPeriodicReport(BLEScan::Impl *impl, uint16_t syncHandle, int8_t rssi, int8_t txPower, const uint8_t *data,
                            size_t len) {
  if (impl->callbacks) {
    impl->callbacks->onPeriodicReport(syncHandle, rssi, txPower, data, len);
  }
  if (impl->periodicReportCb) impl->periodicReportCb(syncHandle, rssi, txPower, data, len);
}

void dispatchPeriodicLost(BLEScan::Impl *impl, uint16_t syncHandle) {
  if (impl->callbacks) {
    impl->callbacks->onPeriodicLost(syncHandle);
  }
  if (impl->periodicLostCb) impl->periodicLostCb(syncHandle);
}
#endif

} // namespace

BLEAdvertisedDevice BLEScan::Impl::parseDiscEvent(const struct ble_gap_disc_desc *disc) {
  auto impl = std::make_shared<BLEAdvertisedDevice::Impl>();
  impl->address = BTAddress(disc->addr.val, static_cast<BTAddress::Type>(disc->addr.type));
  impl->addrType = static_cast<BTAddress::Type>(disc->addr.type);
  impl->rssi = disc->rssi;
  impl->hasRSSI = true;
  impl->legacy = true;

  switch (disc->event_type) {
    case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND:
      impl->connectable = true; impl->scannable = true;
      impl->advType = BLEAdvType::ConnectableScannable; break;
    case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND:
      impl->connectable = true; impl->directed = true;
      impl->advType = BLEAdvType::ConnectableDirected; break;
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND:
      impl->scannable = true;
      impl->advType = BLEAdvType::ScannableUndirected; break;
    case BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND:
      impl->advType = BLEAdvType::NonConnectable; break;
    default: break;
  }

  if (disc->length_data > 0 && disc->data != nullptr) {
    impl->parsePayload(disc->data, disc->length_data);
  }

  return BLEAdvertisedDevice(impl);
}

#if CONFIG_BT_NIMBLE_EXT_ADV
BLEAdvertisedDevice BLEScan::Impl::parseExtDiscEvent(const struct ble_gap_ext_disc_desc *disc) {
  auto impl = std::make_shared<BLEAdvertisedDevice::Impl>();
  impl->address = BTAddress(disc->addr.val, static_cast<BTAddress::Type>(disc->addr.type));
  impl->addrType = static_cast<BTAddress::Type>(disc->addr.type);
  impl->rssi = disc->rssi;
  impl->hasRSSI = true;
  impl->legacy = (disc->props & BLE_HCI_ADV_LEGACY_MASK) != 0;
  impl->connectable = (disc->props & BLE_HCI_ADV_CONN_MASK) != 0;
  impl->scannable = (disc->props & BLE_HCI_ADV_SCAN_MASK) != 0;
  impl->directed = (disc->props & BLE_HCI_ADV_DIRECT_MASK) != 0;
  impl->primaryPhy = static_cast<BLEPhy>(disc->prim_phy);
  impl->secondaryPhy = static_cast<BLEPhy>(disc->sec_phy);
  impl->sid = disc->sid;
  impl->periodicInterval = disc->periodic_adv_itvl;
  if (disc->tx_power != 127) {
    impl->txPower = disc->tx_power;
    impl->hasTXPower = true;
  }

  if (disc->length_data > 0 && disc->data != nullptr) {
    impl->parsePayload(disc->data, disc->length_data);
  }

  return BLEAdvertisedDevice(impl);
}
#endif

int BLEScan::Impl::gapEventHandler(struct ble_gap_event *event, void *arg) {
  auto *impl = static_cast<BLEScan::Impl *>(arg);
  if (!impl) return 0;

  switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
      BLEAdvertisedDevice dev = impl->parseDiscEvent(&event->disc);
      dispatchResult(impl, dev);
      appendOrReplaceResult(impl, dev);
      return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE: {
      impl->scanning = false;
      dispatchComplete(impl);
      impl->scanSync.give(BTStatus::OK);
      return 0;
    }

#if CONFIG_BT_NIMBLE_EXT_ADV
    case BLE_GAP_EVENT_EXT_DISC: {
      BLEAdvertisedDevice dev = impl->parseExtDiscEvent(&event->ext_disc);
      dispatchResult(impl, dev);
      appendOrReplaceResult(impl, dev);
      return 0;
    }

    case BLE_GAP_EVENT_PERIODIC_SYNC: {
      if (impl->callbacks || impl->periodicSyncCb) {
        BTAddress addr(event->periodic_sync.adv_addr.val,
                       static_cast<BTAddress::Type>(event->periodic_sync.adv_addr.type));
        dispatchPeriodicSync(impl, event->periodic_sync.sync_handle,
                             event->periodic_sync.sid, addr,
                             static_cast<BLEPhy>(event->periodic_sync.adv_phy),
                             event->periodic_sync.per_adv_ival);
      }
      return 0;
    }

    case BLE_GAP_EVENT_PERIODIC_REPORT: {
      if (impl->callbacks || impl->periodicReportCb) {
        const uint8_t *data = event->periodic_report.om ? OS_MBUF_DATA(event->periodic_report.om, const uint8_t *) : nullptr;
        size_t len = event->periodic_report.om ? OS_MBUF_PKTLEN(event->periodic_report.om) : 0;
        dispatchPeriodicReport(impl, event->periodic_report.sync_handle,
                               event->periodic_report.rssi,
                               event->periodic_report.tx_power,
                               data, len);
      }
      return 0;
    }

    case BLE_GAP_EVENT_PERIODIC_SYNC_LOST: {
      if (impl->callbacks || impl->periodicLostCb) {
        dispatchPeriodicLost(impl, event->periodic_sync_lost.sync_handle);
      }
      return 0;
    }
#endif

    default: return 0;
  }
}

// --------------------------------------------------------------------------
// BLEScan public API
// --------------------------------------------------------------------------

void BLEScan::setInterval(uint16_t intervalMs) {
  BLE_CHECK_IMPL();
  impl.interval = (intervalMs * 1000) / 625;
}

void BLEScan::setWindow(uint16_t windowMs) {
  BLE_CHECK_IMPL();
  impl.window = (windowMs * 1000) / 625;
}

void BLEScan::setActiveScan(bool active) { BLE_CHECK_IMPL(); impl.activeScan = active; }
void BLEScan::setFilterDuplicates(bool filter) { BLE_CHECK_IMPL(); impl.filterDuplicates = filter; }
void BLEScan::clearDuplicateCache() { /* NimBLE manages this internally */ }

BTStatus BLEScan::start(uint32_t durationMs, bool continueExisting) {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (impl.scanning && !continueExisting) {
    stop();
  }

  if (!continueExisting) {
    impl.results._devices.clear();
  }

  struct ble_gap_disc_params params = {};
  params.filter_duplicates = impl.filterDuplicates ? 1 : 0;
  params.passive = impl.activeScan ? 0 : 1;
  params.itvl = impl.interval;
  params.window = impl.window;
  params.filter_policy = 0;
  params.limited = 0;

  int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, durationMs == 0 ? BLE_HS_FOREVER : (int32_t)durationMs, &params,
                        Impl::gapEventHandler, &impl);
  if (rc != 0) {
    log_e("ble_gap_disc: rc=%d", rc);
    return BTStatus::Fail;
  }
  impl.scanning = true;
  return BTStatus::OK;
}

BLEScanResults BLEScan::startBlocking(uint32_t durationMs) {
  BLE_CHECK_IMPL(BLEScanResults());
  impl.results._devices.clear();
  impl.scanSync.take();

  struct ble_gap_disc_params params = {};
  params.filter_duplicates = impl.filterDuplicates ? 1 : 0;
  params.passive = impl.activeScan ? 0 : 1;
  params.itvl = impl.interval;
  params.window = impl.window;

  int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, (int32_t)durationMs, &params,
                        Impl::gapEventHandler, &impl);
  if (rc != 0) {
    impl.scanSync.give(BTStatus::Fail);
    log_e("ble_gap_disc: rc=%d", rc);
    return BLEScanResults();
  }
  impl.scanning = true;
  impl.scanSync.wait(durationMs + 2000);
  return impl.results;
}

BTStatus BLEScan::stop() {
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  if (!impl.scanning) return BTStatus::OK;
  int rc = ble_gap_disc_cancel();
  impl.scanning = false;
  return (rc == 0 || rc == BLE_HS_EALREADY) ? BTStatus::OK : BTStatus::Fail;
}

bool BLEScan::isScanning() const { return _impl && _impl->scanning; }

BLEScanResults BLEScan::getResults() { return _impl ? _impl->results : BLEScanResults(); }

void BLEScan::clearResults() {
  BLE_CHECK_IMPL();
  impl.results._devices.clear();
}

void BLEScan::erase(const BTAddress &address) {
  BLE_CHECK_IMPL();
  auto &devs = impl.results._devices;
  for (auto it = devs.begin(); it != devs.end(); ++it) {
    if (it->getAddress() == address) {
      devs.erase(it);
      break;
    }
  }
}

BTStatus BLEScan::startExtended(uint32_t durationMs, const ExtScanConfig *codedConfig, const ExtScanConfig *uncodedConfig) {
#if CONFIG_BT_NIMBLE_EXT_ADV
  BLE_CHECK_IMPL(BTStatus::InvalidState);
  impl.results._devices.clear();

  struct ble_gap_ext_disc_params uncodedParams = {};
  struct ble_gap_ext_disc_params codedParams = {};
  uint8_t active = impl.activeScan ? 1 : 0;

  uncodedParams.passive = !active;
  uncodedParams.itvl = uncodedConfig ? ((uncodedConfig->interval * 1000) / 625) : impl.interval;
  uncodedParams.window = uncodedConfig ? ((uncodedConfig->window * 1000) / 625) : impl.window;

  codedParams.passive = !active;
  codedParams.itvl = codedConfig ? ((codedConfig->interval * 1000) / 625) : impl.interval;
  codedParams.window = codedConfig ? ((codedConfig->window * 1000) / 625) : impl.window;

  int rc = ble_gap_ext_disc(BLE_OWN_ADDR_PUBLIC,
                            durationMs == 0 ? 0 : (durationMs / 10),
                            0, impl.filterDuplicates ? 1 : 0, 0, 0,
                            &uncodedParams,
                            codedConfig ? &codedParams : NULL,
                            Impl::gapEventHandler, &impl);
  if (rc != 0) {
    log_e("ble_gap_ext_disc: rc=%d", rc);
    return BTStatus::Fail;
  }
  impl.scanning = true;
  return BTStatus::OK;
#else
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEScan::stopExtended() {
  return stop();
}

BTStatus BLEScan::createPeriodicSync(const BTAddress &addr, uint8_t sid, uint16_t skipCount, uint16_t timeoutMs) {
#if CONFIG_BT_NIMBLE_EXT_ADV
  BLE_CHECK_IMPL(BTStatus::InvalidState);

  struct ble_gap_periodic_sync_params params = {};
  params.skip = skipCount;
  params.sync_timeout = timeoutMs / 10;

  ble_addr_t bleAddr;
  bleAddr.type = static_cast<uint8_t>(addr.type());
  memcpy(bleAddr.val, addr.data(), 6);

  int rc = ble_gap_periodic_adv_sync_create(&bleAddr, sid, &params, Impl::gapEventHandler, &impl);
  if (rc != 0) {
    log_e("ble_gap_periodic_adv_sync_create: rc=%d", rc);
    return BTStatus::Fail;
  }
  return BTStatus::OK;
#else
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEScan::cancelPeriodicSync() {
#if CONFIG_BT_NIMBLE_EXT_ADV
  int rc = ble_gap_periodic_adv_sync_create_cancel();
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
#else
  return BTStatus::NotSupported;
#endif
}

BTStatus BLEScan::terminatePeriodicSync(uint16_t syncHandle) {
#if CONFIG_BT_NIMBLE_EXT_ADV
  int rc = ble_gap_periodic_adv_sync_terminate(syncHandle);
  return (rc == 0) ? BTStatus::OK : BTStatus::Fail;
#else
  return BTStatus::NotSupported;
#endif
}

// --------------------------------------------------------------------------
// BLEClass::getScan() -- NimBLE factory method
// --------------------------------------------------------------------------

BLEScan BLEClass::getScan() {
  if (!isInitialized()) {
    return BLEScan();
  }
  static std::shared_ptr<BLEScan::Impl> scanImpl;
  if (!scanImpl) {
    scanImpl = std::make_shared<BLEScan::Impl>();
  }
  return BLEScan(scanImpl);
}

#endif /* (SOC_BLE_SUPPORTED || CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) && CONFIG_NIMBLE_ENABLED */
