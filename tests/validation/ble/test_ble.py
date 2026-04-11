"""
Combined BLE hardware validation.

Phases run sequentially on two DUTs (server + client).
Single test function, one upload; each phase recorded via record_property
so sub-results appear individually in the final report.
"""

import logging

import pytest

from conftest import rand_str4

LOGGER = logging.getLogger(__name__)

PHASE_LABELS = {
    1: "basic_lifecycle",
    2: "ble5_ext_periodic_adv",
    3: "gatt_setup_connect",
    4: "gatt_read_write",
    5: "notifications_indications",
    6: "large_att_write",
    7: "security",
    8: "ble5_phy_dle",
    9: "reconnect",
    10: "memory_release",
}


# ---------------------------------------------------------------------------
# Phase helpers
# ---------------------------------------------------------------------------

def _phase_basic(server, client):
    server.expect_exact("[SERVER] Init OK", timeout=30)
    server.expect_exact("[SERVER] Deinit OK", timeout=10)
    server.expect_exact("[SERVER] Reinit OK", timeout=30)
    server.expect_exact("[SERVER] Final deinit OK", timeout=10)

    client.expect_exact("[CLIENT] Init OK", timeout=30)
    client.expect_exact("[CLIENT] Deinit OK", timeout=10)
    client.expect_exact("[CLIENT] Reinit OK", timeout=30)
    client.expect_exact("[CLIENT] Final deinit OK", timeout=10)


def _phase_ble5_adv(server, client):
    """Returns (ble5_server, ble5_client) booleans."""
    m_server = server.expect(
        r"\[SERVER\] (BLE5 init OK|BLE5 not supported, skipping)", timeout=30
    )
    ble5_server = b"BLE5 init OK" in m_server.group(0)

    m_client = client.expect(
        r"\[CLIENT\] (BLE5 init OK|BLE5 not supported, skipping)", timeout=30
    )
    ble5_client = b"BLE5 init OK" in m_client.group(0)

    if ble5_server and ble5_client:
        server.expect_exact("[SERVER] Extended adv configured", timeout=10)
        server.expect_exact("[SERVER] Extended adv started", timeout=10)
        server.expect_exact("[SERVER] Periodic adv started", timeout=10)

        client.expect_exact("[CLIENT] Scanning for periodic adv...", timeout=10)
        client.expect_exact("[CLIENT] Found target via ext scan!", timeout=60)
        client.expect_exact("[CLIENT] Synced to periodic adv!", timeout=60)
        client.expect_exact("[CLIENT] Periodic data received", timeout=30)
        client.expect_exact("[CLIENT] Periodic test complete", timeout=10)

        server.expect_exact("[SERVER] BLE5 adv phase done", timeout=30)
        client.expect_exact("[CLIENT] BLE5 scan phase done", timeout=10)

    return ble5_server, ble5_client


def _phase_gatt_setup_connect(server, client):
    server.expect(r"\[SERVER\] Heap before init: \d+", timeout=30)
    server.expect_exact("[SERVER] GATT init OK", timeout=30)
    server.expect(r"\[SERVER\] Heap after init: \d+", timeout=10)
    server.expect_exact("[SERVER] Security configured", timeout=10)
    server.expect(r"\[SERVER\] Heap after server: \d+", timeout=10)
    server.expect_exact("[SERVER] Server started", timeout=10)
    server.expect_exact("[SERVER] Advertising started", timeout=10)

    client.expect(r"\[CLIENT\] Heap before init: \d+", timeout=30)
    client.expect_exact("[CLIENT] GATT init OK", timeout=30)
    client.expect(r"\[CLIENT\] Heap after init: \d+", timeout=10)
    client.expect(r"\[CLIENT\] Scanning \(attempt \d+\)\.\.\.", timeout=10)
    client.expect_exact("[CLIENT] Found target server!", timeout=60)
    client.expect_exact("[CLIENT] Connected", timeout=20)
    server.expect_exact("[SERVER] Client connected", timeout=10)

    client.expect(r"\[CLIENT\] Connect time: \d+ ms", timeout=10)
    client.expect(r"\[CLIENT\] Negotiated MTU: \d+", timeout=10)
    client.expect(r"\[CLIENT\] Heap after connect: \d+", timeout=10)


def _phase_gatt_rw(server, client):
    client.expect_exact("[CLIENT] Found service", timeout=10)
    client.expect_exact("[CLIENT] Found characteristic", timeout=10)

    client.expect_exact("[CLIENT] Read value: Hello from server!", timeout=10)
    client.expect(r"\[CLIENT\] Read latency: \d+ us", timeout=10)
    client.expect_exact("[CLIENT] Write OK", timeout=10)
    server.expect_exact("[SERVER] Received write: Hello from client!", timeout=10)
    client.expect_exact("[CLIENT] Read-back: Hello from client!", timeout=10)


def _phase_notifications(server, client):
    client.expect_exact("[CLIENT] Subscribed to notifications", timeout=10)
    client.expect_exact("[CLIENT] Subscribed to indications", timeout=10)
    client.expect_exact("[CLIENT] Notification received: notify_test_1", timeout=30)
    client.expect_exact("[CLIENT] Indication received: indicate_test_1", timeout=30)
    client.expect_exact("[CLIENT] Unsubscribed", timeout=15)

    server.expect_exact("[SERVER] Subscriber count: 1", timeout=10)
    server.expect_exact("[SERVER] Notification sent", timeout=10)
    server.expect_exact("[SERVER] Indication sent", timeout=10)
    server.expect_exact("[SERVER] Subscriber count: 0", timeout=10)


def _phase_large_write(server, client):
    client.expect_exact("[CLIENT] Large write OK", timeout=30)
    server.expect_exact("[SERVER] Received 512 bytes", timeout=10)
    client.expect_exact("[CLIENT] Large read (512 bytes)", timeout=30)
    client.expect_exact("[CLIENT] Large data integrity OK", timeout=10)


def _phase_security(server, client):
    m_server = server.expect(r"\[SERVER\] Passkey: (\d+)", timeout=30)
    server_pin = m_server.group(1).decode()
    m_client = client.expect(r"\[CLIENT\] Passkey: (\d+)", timeout=30)
    client_pin = m_client.group(1).decode()
    assert server_pin == client_pin, f"PIN mismatch: {server_pin} vs {client_pin}"

    server.expect_exact("[SERVER] Authentication complete", timeout=30)
    client.expect_exact("[CLIENT] Authentication complete", timeout=30)
    client.expect_exact("[CLIENT] Secure read: Secure Data!", timeout=10)


def _phase_ble5_phy_dle(client):
    """Returns True if BLE5 PHY/DLE ran, False if skipped."""
    m = client.expect(
        r"\[CLIENT\] (PHY update OK|BLE5 PHY/DLE not supported, skipping)",
        timeout=10,
    )
    if b"PHY update OK" in m.group(0):
        client.expect_exact("[CLIENT] DLE set OK", timeout=10)
        return True
    return False


def _phase_reconnect(server, client):
    client.expect_exact("[CLIENT] Disconnected for reconnect test", timeout=10)
    server.expect_exact("[SERVER] Client disconnected", timeout=10)

    for i in range(3):
        client.expect_exact(f"[CLIENT] Connect cycle {i + 1}", timeout=30)
        client.expect_exact("[CLIENT] Connected", timeout=20)
        server.expect_exact("[SERVER] Client connected", timeout=10)
        client.expect_exact("[CLIENT] Disconnected", timeout=10)
        server.expect_exact("[SERVER] Client disconnected", timeout=10)

    client.expect_exact("[CLIENT] All cycles complete", timeout=10)


def _phase_memory_release(server, client):
    MIN_FREED = 10240

    for tag, dut in [("SERVER", server), ("CLIENT", client)]:
        dut.expect(rf"\[{tag}\] Heap before release: \d+", timeout=30)
        dut.expect(rf"\[{tag}\] Heap after release: \d+", timeout=10)
        m = dut.expect(rf"\[{tag}\] Memory freed: (-?\d+) bytes", timeout=10)
        freed = int(m.group(1))
        assert freed >= MIN_FREED, (
            f"{tag}: expected >= {MIN_FREED} bytes freed, got {freed}"
        )
        dut.expect_exact(f"[{tag}] Memory release OK", timeout=10)
        dut.expect_exact(f"[{tag}] Reinit blocked OK", timeout=10)
        dut.expect_exact(f"[{tag}] All phases complete", timeout=10)


# ---------------------------------------------------------------------------
# Single test function — one upload, sub-results via record_property
# ---------------------------------------------------------------------------

def test_ble(dut, ci_job_id, record_property):
    server = dut[0]
    client = dut[1]

    name = "BLE_" + ci_job_id if ci_job_id else "BLE_" + rand_str4()
    LOGGER.info("BLE combined test name: %s", name)

    # ---- Name exchange ----
    server.expect_exact("[SERVER] Device ready for name", timeout=120)
    client.expect_exact("[CLIENT] Device ready for name", timeout=120)
    server.expect_exact("[SERVER] Send name:")
    client.expect_exact("[CLIENT] Send name:")
    server.write(f"{name}")
    client.write(f"{name}")
    server.expect_exact(f"[SERVER] Name: {name}", timeout=10)
    client.expect_exact(f"[CLIENT] Target: {name}", timeout=10)

    passed = []
    failed = []

    # Phase 1
    label = PHASE_LABELS[1]
    LOGGER.info("Running phase 1: %s", label)
    try:
        _phase_basic(server, client)
        passed.append(label)
        record_property(f"phase_{label}", "PASS")
        LOGGER.info("PASSED: phase 1 (%s)", label)
    except Exception as e:
        failed.append((label, str(e)))
        record_property(f"phase_{label}", f"FAIL: {e}")
        LOGGER.error("FAILED: phase 1 (%s): %s", label, e)
        pytest.fail(f"phase 1 ({label}) failed: {e}")

    # Phase 2
    label = PHASE_LABELS[2]
    LOGGER.info("Running phase 2: %s", label)
    try:
        ble5_srv, ble5_cli = _phase_ble5_adv(server, client)
        if ble5_srv and ble5_cli:
            passed.append(label)
            record_property(f"phase_{label}", "PASS")
            LOGGER.info("PASSED: phase 2 (%s)", label)
        else:
            record_property(f"phase_{label}", "SKIP: BLE5 not supported")
            LOGGER.info("SKIPPED: phase 2 (%s): BLE5 not supported", label)
    except Exception as e:
        failed.append((label, str(e)))
        record_property(f"phase_{label}", f"FAIL: {e}")
        LOGGER.error("FAILED: phase 2 (%s): %s", label, e)
        pytest.fail(f"phase 2 ({label}) failed: {e}")

    # Phase 3
    label = PHASE_LABELS[3]
    LOGGER.info("Running phase 3: %s", label)
    try:
        _phase_gatt_setup_connect(server, client)
        passed.append(label)
        record_property(f"phase_{label}", "PASS")
        LOGGER.info("PASSED: phase 3 (%s)", label)
    except Exception as e:
        failed.append((label, str(e)))
        record_property(f"phase_{label}", f"FAIL: {e}")
        LOGGER.error("FAILED: phase 3 (%s): %s", label, e)
        pytest.fail(f"phase 3 ({label}) failed: {e}")

    # Phase 4
    label = PHASE_LABELS[4]
    LOGGER.info("Running phase 4: %s", label)
    try:
        _phase_gatt_rw(server, client)
        passed.append(label)
        record_property(f"phase_{label}", "PASS")
        LOGGER.info("PASSED: phase 4 (%s)", label)
    except Exception as e:
        failed.append((label, str(e)))
        record_property(f"phase_{label}", f"FAIL: {e}")
        LOGGER.error("FAILED: phase 4 (%s): %s", label, e)
        pytest.fail(f"phase 4 ({label}) failed: {e}")

    # Phase 5
    label = PHASE_LABELS[5]
    LOGGER.info("Running phase 5: %s", label)
    try:
        _phase_notifications(server, client)
        passed.append(label)
        record_property(f"phase_{label}", "PASS")
        LOGGER.info("PASSED: phase 5 (%s)", label)
    except Exception as e:
        failed.append((label, str(e)))
        record_property(f"phase_{label}", f"FAIL: {e}")
        LOGGER.error("FAILED: phase 5 (%s): %s", label, e)
        pytest.fail(f"phase 5 ({label}) failed: {e}")

    # Phase 6
    label = PHASE_LABELS[6]
    LOGGER.info("Running phase 6: %s", label)
    try:
        _phase_large_write(server, client)
        passed.append(label)
        record_property(f"phase_{label}", "PASS")
        LOGGER.info("PASSED: phase 6 (%s)", label)
    except Exception as e:
        failed.append((label, str(e)))
        record_property(f"phase_{label}", f"FAIL: {e}")
        LOGGER.error("FAILED: phase 6 (%s): %s", label, e)
        pytest.fail(f"phase 6 ({label}) failed: {e}")

    # Phase 7
    label = PHASE_LABELS[7]
    LOGGER.info("Running phase 7: %s", label)
    try:
        _phase_security(server, client)
        passed.append(label)
        record_property(f"phase_{label}", "PASS")
        LOGGER.info("PASSED: phase 7 (%s)", label)
    except Exception as e:
        failed.append((label, str(e)))
        record_property(f"phase_{label}", f"FAIL: {e}")
        LOGGER.error("FAILED: phase 7 (%s): %s", label, e)
        pytest.fail(f"phase 7 ({label}) failed: {e}")

    # Phase 8
    label = PHASE_LABELS[8]
    LOGGER.info("Running phase 8: %s", label)
    try:
        phy_ok = _phase_ble5_phy_dle(client)
        if phy_ok:
            passed.append(label)
            record_property(f"phase_{label}", "PASS")
            LOGGER.info("PASSED: phase 8 (%s)", label)
        else:
            record_property(f"phase_{label}", "SKIP: BLE5 not supported")
            LOGGER.info("SKIPPED: phase 8 (%s): BLE5 not supported", label)
    except Exception as e:
        failed.append((label, str(e)))
        record_property(f"phase_{label}", f"FAIL: {e}")
        LOGGER.error("FAILED: phase 8 (%s): %s", label, e)
        pytest.fail(f"phase 8 ({label}) failed: {e}")

    # Phase 9
    label = PHASE_LABELS[9]
    LOGGER.info("Running phase 9: %s", label)
    try:
        _phase_reconnect(server, client)
        passed.append(label)
        record_property(f"phase_{label}", "PASS")
        LOGGER.info("PASSED: phase 9 (%s)", label)
    except Exception as e:
        failed.append((label, str(e)))
        record_property(f"phase_{label}", f"FAIL: {e}")
        LOGGER.error("FAILED: phase 9 (%s): %s", label, e)
        pytest.fail(f"phase 9 ({label}) failed: {e}")

    # Phase 10
    label = PHASE_LABELS[10]
    LOGGER.info("Running phase 10: %s", label)
    try:
        _phase_memory_release(server, client)
        passed.append(label)
        record_property(f"phase_{label}", "PASS")
        LOGGER.info("PASSED: phase 10 (%s)", label)
    except Exception as e:
        failed.append((label, str(e)))
        record_property(f"phase_{label}", f"FAIL: {e}")
        LOGGER.error("FAILED: phase 10 (%s): %s", label, e)

    summary = f"{len(passed)} passed, {len(failed)} failed out of {len(PHASE_LABELS)}"
    LOGGER.info("Summary: %s", summary)
    record_property("summary", summary)

    if failed:
        lines = [f"  {lbl}: {err}" for lbl, err in failed]
        pytest.fail(f"{len(failed)} phase(s) failed:\n" + "\n".join(lines))
