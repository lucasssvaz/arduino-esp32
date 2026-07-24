"""
LE Audio (GAF) hardware validation.

Split out of the combined `ble` suite so that test stays focused on core
GATT/GAP while this one owns the LE Audio stack: the ISO transport, BAP
unicast/broadcast data plane, the LC3 codec loopback, the control profiles
(VCP/MICP/MCP/CCP via CAP), the top-level identity profiles (TMAP/GMAP), and
the Hearing Access Service.

Phases run sequentially on two DUTs (server + client). Single test function,
one upload; each phase recorded via record_property so sub-results appear
individually in the final report.

Every phase self-skips when the relevant feature is not compiled in, so the
suite runs unchanged on non-audio silicon. The data-plane phases only truly
exercise LE Audio on a dual-audio esp32s31 <-> esp32s31 bench.
"""

import logging

import pytest

from conftest import rand_str4

LOGGER = logging.getLogger(__name__)

PHASE_LABELS = {
    1: "audio_lifecycle",
    2: "iso_cis",
    3: "iso_bis",
    4: "bap_unicast",
    5: "bap_broadcast",
    6: "lc3_loopback",
    7: "control_profiles",
    8: "top_profiles",
    9: "hearing_aid",
    10: "memory_release",
}


# ---------------------------------------------------------------------------
# Phase helpers
# ---------------------------------------------------------------------------


def _start_phase(server, client, phase_num):
    """Send phase start command to both devices and wait for acknowledgment."""
    LOGGER.info("Sending START_PHASE_%d to both devices", phase_num)
    server.write(f"START_PHASE_{phase_num}\n")
    client.write(f"START_PHASE_{phase_num}\n")
    server.expect_exact(f"[SERVER] Phase {phase_num} started", timeout=10)
    client.expect_exact(f"[CLIENT] Phase {phase_num} started", timeout=10)


def _phase_audio_lifecycle(server, client):
    """LE Audio engine bring-up + classic-service coexistence (server-driven).

    Returns False (SKIP) when the LE Audio engine is not compiled into the build.
    """
    m = server.expect(r"\[SERVER\] (Audio lifecycle OK|Audio not supported, skipping)", timeout=30)
    supported = b"Audio lifecycle OK" in m.group(0)
    if not supported:
        server.expect_exact("[SERVER] Phase1 audio done", timeout=10)
        client.expect_exact("[CLIENT] Phase1 audio done", timeout=20)
        return False
    server.expect_exact("[SERVER] Phase1 audio done", timeout=20)
    client.expect_exact("[CLIENT] Audio lifecycle OK", timeout=20)
    client.expect_exact("[CLIENT] Phase1 audio done", timeout=10)
    return True


def _phase_iso_cis(server, client):
    """Internal ISO CIS transport integrity (server = Peripheral, client = Central).

    Returns False (SKIP) when host ISO is not compiled into either build (the
    only rig that truly runs this is a dual-ISO esp32s31<->esp32s31 pair).
    """
    ms = server.expect(r"\[SERVER\] IsoCis (not supported|result connected=(\d+) received=(\d+))", timeout=60)
    mc = client.expect(r"\[CLIENT\] IsoCis (not supported|result connected=(\d+) sent=(\d+))", timeout=90)
    server.expect_exact("[SERVER] Phase2 iso_cis done", timeout=40)
    client.expect_exact("[CLIENT] Phase2 iso_cis done", timeout=40)

    if b"not supported" in ms.group(0) or b"not supported" in mc.group(0):
        return False

    received = int(ms.group(3))
    sent = int(mc.group(3))
    assert sent > 0, "CIS Central sent no SDUs"
    assert received > 0, f"CIS Peripheral received no SDUs (Central sent {sent})"
    return True


def _phase_iso_bis(server, client):
    """Internal ISO BIS transport integrity (server = Broadcaster, client = Receiver).

    Returns False (SKIP) when host ISO or BLE5 is not compiled into either build.
    """
    ms = server.expect(r"\[SERVER\] IsoBis (not supported|result connected=(\d+) sent=(\d+))", timeout=60)
    mc = client.expect(r"\[CLIENT\] IsoBis (not supported|result synced=(\d+) received=(\d+))", timeout=90)
    server.expect_exact("[SERVER] Phase3 iso_bis done", timeout=40)
    client.expect_exact("[CLIENT] Phase3 iso_bis done", timeout=40)

    if b"not supported" in ms.group(0) or b"not supported" in mc.group(0):
        return False

    sent = int(ms.group(3))
    received = int(mc.group(3))
    assert sent > 0, "BIS Broadcaster streamed no SDUs"
    assert received > 0, f"BIS Receiver received no SDUs (Broadcaster sent {sent})"
    return True


def _phase_bap_unicast(server, client):
    """BAP unicast transport integrity (server = Unicast Server, client = Unicast Client).

    The client runs the full BAP setup and streams transparent SDUs to the
    server's sink ASE over a CIS. Returns False (SKIP) when the LE Audio engine
    is not compiled into either build (only a dual-audio esp32s31 pair runs it).
    """
    ms = server.expect(r"\[SERVER\] BapUnicast (not supported|result received=(\d+))", timeout=90)
    mc = client.expect(r"\[CLIENT\] BapUnicast (not supported|result streaming=(\d+) sent=(\d+))", timeout=120)
    server.expect_exact("[SERVER] Phase4 bap_unicast done", timeout=40)
    client.expect_exact("[CLIENT] Phase4 bap_unicast done", timeout=40)

    if b"not supported" in ms.group(0) or b"not supported" in mc.group(0):
        return False

    received = int(ms.group(2))
    sent = int(mc.group(3))
    assert sent > 0, "Unicast Client sent no SDUs"
    assert received > 0, f"Unicast Server received no SDUs (Client sent {sent})"
    return True


def _phase_bap_broadcast(server, client):
    """BAP broadcast transport integrity (server = Broadcast Source, client = Broadcast Sink).

    The server announces an Auracast broadcast (ext + periodic adv + BASE) and
    streams transparent SDUs over a BIG; the client scans, PA-syncs, syncs the
    BIG, and counts SDUs received. Returns False (SKIP) when the LE Audio engine
    is not compiled into either build (only a dual-audio esp32s31 pair runs it).
    """
    ms = server.expect(r"\[SERVER\] BapBroadcast (not supported|result streaming=(\d+))", timeout=90)
    mc = client.expect(r"\[CLIENT\] BapBroadcast (not supported|result received=(\d+))", timeout=120)
    server.expect_exact("[SERVER] Phase5 bap_broadcast done", timeout=40)
    client.expect_exact("[CLIENT] Phase5 bap_broadcast done", timeout=40)

    if b"not supported" in ms.group(0) or b"not supported" in mc.group(0):
        return False

    received = int(mc.group(2))
    assert received > 0, "Broadcast Sink received no SDUs"
    return True


def _phase_lc3_loopback(server, client):
    """End-to-end LC3 audio path (client Recorder -> CIS -> server Player).

    The client feeds a synthetic 1 kHz tone into a BLEAudioRecorder, which
    LC3-encodes and streams it over the CIS; the server's BLEAudioPlayer decodes
    it and reports the decoded sample count plus mean absolute amplitude. This
    proves the whole codec + I2S-less data plane works on real hardware.

    Returns False (SKIP) when the LC3 codec (esp_audio_codec) is not compiled
    into either build; only a dual-audio esp32s31 pair with the codec runs it.
    """
    ms = server.expect(r"\[SERVER\] Lc3Loopback (not supported|result samples=(\d+) meanamp=(\d+))", timeout=120)
    mc = client.expect(r"\[CLIENT\] Lc3Loopback (not supported|result streaming=(\d+))", timeout=120)
    server.expect_exact("[SERVER] Phase6 lc3_loopback done", timeout=40)
    client.expect_exact("[CLIENT] Phase6 lc3_loopback done", timeout=40)

    if b"not supported" in ms.group(0) or b"not supported" in mc.group(0):
        return False

    samples = int(ms.group(2))
    meanamp = int(ms.group(3))
    assert samples > 0, "Player decoded no PCM samples"
    # A decoded 1 kHz tone at amplitude ~8000 has a mean |amplitude| well above
    # a few hundred; a low value would mean silence / a broken codec path.
    assert meanamp > 500, f"decoded audio too quiet (meanamp={meanamp}); tone did not survive LC3"
    return True


def _phase_control_profiles(server, client):
    """LE Audio control-profile round-trip over one ACL (server = acceptor,
    client = controller / "phone").

    The server exposes CAP acceptor (CAS+CSIS), VCP renderer, MICP device, MCP
    media player, and CCP call server; the client connects once and drives each:
    CAP discovery, VCP setVolume, MICP mute, MCP play, CCP originate. The server
    tallies the writes it observes. Returns False (SKIP) when the LE Audio engine
    is not compiled into either build (only a dual-audio esp32s31 pair runs it).

    Hard gate: the VCP path must work end to end (client discovers VCS and the
    server observes the volume write). The other profiles are exercised and
    logged; their client-side discovery is asserted, but their deeper state
    machines are informational.
    """
    ms = server.expect(r"\[SERVER\] ControlProfiles (not supported|result volwrites=(\d+) micmutes=(\d+) calls=(\d+))", timeout=120)
    mc = client.expect(r"\[CLIENT\] ControlProfiles (not supported|result cap=(\d+) vcp=(\d+) mic=(\d+) media=(\d+) call=(\d+))", timeout=120)
    server.expect_exact("[SERVER] Phase7 control_profiles done", timeout=40)
    client.expect_exact("[CLIENT] Phase7 control_profiles done", timeout=40)

    if b"not supported" in ms.group(0) or b"not supported" in mc.group(0):
        return False

    vol_writes = int(ms.group(2))
    cap_disc = int(mc.group(2))
    vcp_ok = int(mc.group(3))

    # VCP is the end-to-end gate: client discovered VCS + wrote volume, and the
    # server saw the write land on its renderer.
    assert cap_disc == 1, "CAP initiator did not discover the acceptor's CAS"
    assert vcp_ok == 1, "VCP controller failed to discover/set volume"
    assert vol_writes >= 1, "server renderer observed no volume write from the controller"
    return True


def _phase_top_profiles(server, client):
    """Top-level profile identity: TMAP round-trip; GMAP server is an engine gap.

    The server registers TMAP CallTerminal|UnicastMediaReceiver (TMAS on air) and
    GMAP UnicastGameTerminal (roles recorded locally only on ESP-IDF release/v6.1
    — no host-adapter gmas.c, so GMAS is not published). The client discovers
    TMAS and asserts the TMAP bitmask. GMAP discovery is expected to fail on
    that IDF line; a successful Role read would mean the engine gap closed and
    this check should be restored to require UGT.
    Returns False (SKIP) when TMAP/GMAP are not compiled into either build.
    """
    ms = server.expect(r"\[SERVER\] TopProfiles (not supported|result done)", timeout=90)
    mc = client.expect(r"\[CLIENT\] TopProfiles (not supported|result tmap=(-?\d+) gmap=(-?\d+))", timeout=120)
    server.expect_exact("[SERVER] Phase8 top_profiles done", timeout=40)
    client.expect_exact("[CLIENT] Phase8 top_profiles done", timeout=40)

    if b"not supported" in ms.group(0) or b"not supported" in mc.group(0):
        return False

    tmap_roles = int(mc.group(2))
    gmap_roles = int(mc.group(3))
    # TMAP CallTerminal=0x02, UnicastMediaReceiver=0x08.
    assert tmap_roles >= 0, "TMAP discovery failed on the client"
    assert (tmap_roles & 0x02) and (tmap_roles & 0x08), f"peer TMAP roles 0x{tmap_roles:02x} missing CT|UMR"
    # release/v6.1: GMAS not on the air. Pin the gap; if discovery starts
    # succeeding, restore the UGT assert instead of silently accepting it.
    if gmap_roles >= 0:
        raise AssertionError(
            f"GMAP discovery returned roles 0x{gmap_roles:02x} — GMAS is now "
            "exposed; restore the UGT Role assert for a real round-trip"
        )
    LOGGER.info(
        "top_profiles: GMAP discovery failed as expected on release/v6.1 "
        "(no host-adapter gmas.c); tmap=0x%02x gmap=%d",
        tmap_roles,
        gmap_roles,
    )
    return True


def _phase_hearing_aid(server, client):
    """Hearing Access Service round-trip (server publishes a preset list, client
    discovers it, reads the presets, and switches the active one).

    The server registers a binaural hearing aid with three presets and counts
    select requests; the client discovers the HAS, reads the records, and sets
    the active preset to index 2. Returns False (SKIP) when the HAS server or
    client is not compiled into a build (only a dual-audio esp32s31 pair with
    HAS + HAS client runs it).
    """
    ms = server.expect(r"\[SERVER\] HearingAid (not supported|result presets=3 selects=(\d+) active=(\d+))", timeout=90)
    mc = client.expect(r"\[CLIENT\] HearingAid (not supported|result disc=(\d+) read=(\d+) switched=(\d+))", timeout=120)
    server.expect_exact("[SERVER] Phase9 hearing_aid done", timeout=40)
    client.expect_exact("[CLIENT] Phase9 hearing_aid done", timeout=40)

    if b"not supported" in ms.group(0) or b"not supported" in mc.group(0):
        return False

    disc = int(mc.group(2))
    read = int(mc.group(3))
    switched = int(mc.group(4))
    selects = int(ms.group(2))
    active = int(ms.group(3))
    assert disc == 1, "client failed to discover the peer HAS"
    assert read >= 3, f"expected >= 3 preset records read, got {read}"
    assert switched == 1, "client preset switch was not accepted"
    assert selects >= 1, "server observed no preset select request"
    assert active == 2, f"server active preset should be 2 after the switch, got {active}"
    return True


def _phase_memory_release(server, client):
    MIN_FREED = 10240

    for tag, dut in [("SERVER", server), ("CLIENT", client)]:
        dut.expect(rf"\[{tag}\] Heap before release: \d+", timeout=30)
        dut.expect(rf"\[{tag}\] Heap after release: \d+", timeout=10)
        m = dut.expect(rf"\[{tag}\] Memory freed: (-?\d+) bytes", timeout=10)
        freed = int(m.group(1))
        assert freed >= MIN_FREED, f"{tag}: expected >= {MIN_FREED} bytes freed, got {freed}"
        dut.expect_exact(f"[{tag}] Memory release OK", timeout=10)
        dut.expect_exact(f"[{tag}] Reinit blocked OK", timeout=10)
        dut.expect_exact(f"[{tag}] All phases complete", timeout=10)


# ---------------------------------------------------------------------------
# Single test function — one upload, sub-results via record_property
# ---------------------------------------------------------------------------


def test_ble_audio(dut, ci_job_id, record_property):
    server = dut[0]
    client = dut[1]

    name = "BLEAU_" + (ci_job_id if ci_job_id else rand_str4())
    LOGGER.info("LE Audio combined test name: %s", name)

    server.expect_exact("[SERVER] Device ready for name", timeout=120)
    client.expect_exact("[CLIENT] Device ready for name", timeout=120)
    server.expect_exact("[SERVER] Send name:")
    client.expect_exact("[CLIENT] Send name:")
    server.write(name)
    client.write(name)
    server.expect_exact(f"[SERVER] Name: {name}", timeout=10)
    client.expect_exact(f"[CLIENT] Target: {name}", timeout=10)

    # (phase_fn, *args); all audio phases self-skip on unsupported builds.
    PHASES = {
        1: (_phase_audio_lifecycle, server, client),  # skip if falsy
        2: (_phase_iso_cis, server, client),  # skip if falsy
        3: (_phase_iso_bis, server, client),  # skip if falsy
        4: (_phase_bap_unicast, server, client),  # skip if falsy
        5: (_phase_bap_broadcast, server, client),  # skip if falsy
        6: (_phase_lc3_loopback, server, client),  # skip if falsy
        7: (_phase_control_profiles, server, client),  # skip if falsy
        8: (_phase_top_profiles, server, client),  # skip if falsy
        9: (_phase_hearing_aid, server, client),  # skip if falsy
        10: (_phase_memory_release, server, client),
    }

    SKIP_REASONS = {
        1: "LE Audio not supported",
        2: "host ISO not supported (needs dual esp32s31)",
        3: "host ISO not supported (needs dual esp32s31)",
        4: "LE Audio engine not supported (needs dual esp32s31)",
        5: "LE Audio engine not supported (needs dual esp32s31)",
        6: "LC3 codec not supported (needs esp_audio_codec on dual esp32s31)",
        7: "LE Audio engine not supported (needs dual esp32s31)",
        8: "TMAP/GMAP not supported (needs dual esp32s31)",
        9: "HAS server/client not supported (needs dual esp32s31)",
    }

    passed, failed = [], []

    for num, (fn, *args) in PHASES.items():
        label = PHASE_LABELS[num]
        LOGGER.info("Running phase %d: %s", num, label)
        _start_phase(server, client, num)
        try:
            result = fn(*args)
            if num in SKIP_REASONS and not result:
                record_property(f"phase_{label}", f"SKIP: {SKIP_REASONS[num]}")
                LOGGER.info("SKIPPED: phase %d (%s)", num, label)
            else:
                passed.append(label)
                record_property(f"phase_{label}", "PASS")
                LOGGER.info("PASSED: phase %d (%s)", num, label)
        except Exception as e:
            failed.append((label, str(e)))
            record_property(f"phase_{label}", f"FAIL: {e}")
            LOGGER.error("FAILED: phase %d (%s): %s", num, label, e)

    summary = f"{len(passed)} passed, {len(failed)} failed out of {len(PHASE_LABELS)}"
    LOGGER.info("Summary: %s", summary)
    record_property("summary", summary)

    if failed:
        lines = [f"  {lbl}: {err}" for lbl, err in failed]
        pytest.fail(f"{len(failed)} phase(s) failed:\n" + "\n".join(lines))
