# LE Audio Validation Test

Validates the LE Audio (Generic Audio Framework) stack between a server and a
client across two DUTs: the internal ISO transport, the BAP unicast/broadcast
data plane, the LC3 codec loopback, the control profiles (VCP/MICP/MCP/CCP via
CAP), the top-level identity profiles (TMAP/GMAP), and the Hearing Access
Service. This is a **multi-DUT** test supporting both Bluedroid and NimBLE
stacks.

It was split out of the combined [`ble`](../ble) suite so that test stays
focused on core GATT/GAP while this one owns the GAF stack. The core-BLE
lifecycle (init/deinit, GATT, security, advertising, L2CAP, etc.) lives in
`ble`; this suite assumes a working stack and drives only the audio layer.

## Architecture

```
 ┌──────────────┐          BLE / ISO         ┌──────────────┐
 │    Server    │◄──── advertising ──────────│    Client    │
 │  (device0)   │   scan/connect + CIS/BIS   │  (device1)   │
 └──────┬───────┘                            └──────┬───────┘
        │ serial                                    │ serial
        ▼                                           ▼
 ┌─────────────────────────────────────────────────────────┐
 │                pytest (test_ble_audio.py)                 │
 └─────────────────────────────────────────────────────────┘
```

## Test Cases

The suite runs as a sequence of numbered phases driven by `test_ble_audio.py`.
Each phase is gated by a `START_PHASE_<n>` handshake and recorded individually.
Every audio phase self-skips on targets/builds without the relevant feature, so
the suite runs unchanged on non-audio silicon.

| Phase | Label | What it covers |
|---|---|---|
| 1 | audio_lifecycle | LE Audio engine bring-up (`common_init`/`start`) + classic-service coexistence committed into one GATT table via `BLEGattDatabase` |
| 2 | iso_cis | internal ISO CIS transport integrity: server = CIS Peripheral (`listenCis`), client = CIS Central (`connectCis`); counts transparent SDUs streamed Central->Peripheral (host ISO) |
| 3 | iso_bis | internal ISO BIS transport integrity: server = BIS Broadcaster (`createBig` on ext/periodic adv), client = BIS Receiver (`syncBig` via forwarded BIGInfo); counts SDUs Broadcaster->Receiver (host ISO + BLE5) |
| 4 | bap_unicast | BAP unicast transport integrity: server = Unicast Server (PACS/ASCS, coexisting with a classic GATT service), client = Unicast Client running the full discover->config->QoS->enable->CIS->start setup; counts transparent SDUs streamed Client->Server sink ASE (LE Audio engine) |
| 5 | bap_broadcast | BAP broadcast transport integrity: server = Broadcast Source (ext/periodic adv + BASE, streaming a BIG), client = Broadcast Sink (Scan Delegator/BASS) scanning + PA-syncing + BIG-syncing; counts transparent SDUs streamed Source->Sink over the BIS (LE Audio engine + BLE5) |
| 6 | lc3_loopback | turnkey LC3 data plane end-to-end: client = Unicast Client feeding a 1 kHz tone into a `BLEAudioRecorder` (PCM->LC3->CIS), server = Unicast Server decoding via a `BLEAudioPlayer` (CIS->LC3->PCM); asserts the decoded sample count and mean amplitude prove real audio survived the codec (LC3 codec `esp_audio_codec`) |
| 7 | control_profiles | LE Audio control-profile round-trip over one ACL: server = acceptor exposing CAP (CAS+CSIS), VCP renderer, MICP device, MCP media player (MCS), CCP call server (GTBS); client = controller ("phone") that connects once and drives CAP discovery, VCP setVolume, MICP mute, MCP play, and CCP originate; gated on the VCP write landing on the server's renderer (LE Audio engine) |
| 8 | top_profiles | TMAP identity round-trip: server publishes TMAS (CallTerminal + UnicastMediaReceiver); client discovers and asserts the role bitmask. GMAP `setRoles()` is exercised but server GMAS is an ESP-IDF `release/v6.1` engine gap (no host-adapter `gmas.c`); discovery is expected to fail and the phase pins that until libs ship the adapter (LE Audio engine + TMAP/GMAP) |
| 9 | hearing_aid | Hearing Access Service round-trip: server = binaural hearing aid publishing a 3-preset HAS list and counting select requests; client = controller that discovers the HAS, reads the preset records, and switches the active preset to index 2 (LE Audio engine + HAS server/client) |
| 10 | memory_release | heap reclaimed on release; reinit blocked while released |

## Requirements

- **Hardware**: Two boards with BLE support. The audio data-plane phases only
  truly run on a dual-audio `esp32s31 <-> esp32s31` bench; on any other pair
  they self-skip (the suite still runs green).
- **Wokwi/QEMU**: Not supported (multi-device test)
- **CI Runner**: `two_duts`
- **SoC Config**: `CONFIG_SOC_BLE_SUPPORTED=y`

## Serial Protocol

1. Both devices print `Device ready for name`
2. pytest generates a unique name and sends it to both devices
3. Each phase is gated by a `START_PHASE_<n>` handshake; the server acts as the
   audio source/acceptor and the client as the sink/controller
4. Both devices report a per-phase result line that the host asserts

## Notes

- Each phase brings up its own fresh audio stack (`BLE.end()`/`begin()`), so a
  failure in one phase does not leak state into the next.
- The audio_lifecycle phase (1) self-skips on builds without the LE Audio engine
  (`BLE_AUDIO_SUPPORTED == 0`), so the suite runs unchanged on non-audio silicon.
  It is a single-device (server) engine test; the client only acknowledges the phase.
- The iso_cis (2) and iso_bis (3) phases exercise the internal ISO transport across
  two DUTs and self-skip unless *both* DUTs compile host ISO in (`BLE_ISO_SUPPORTED`;
  iso_bis additionally needs BLE5). In the current target matrix that means they SKIP
  on every pair except a dual-ISO `esp32s31 <-> esp32s31` bench, where they verify
  CIS/BIS SDUs actually flow end-to-end.
- The bap_unicast (4) and bap_broadcast (5) phases exercise the BAP data plane across
  two DUTs and self-skip unless *both* DUTs compile the LE Audio engine in
  (`BLE_AUDIO_SUPPORTED`). They SKIP on every pair except a dual-audio
  `esp32s31 <-> esp32s31` bench, where transparent SDUs flow over a real CIS
  (unicast) or BIS (broadcast).
- The lc3_loopback (6) phase exercises the turnkey LC3 data plane
  (`BLEAudioRecorder`/`BLEAudioPlayer`) across two DUTs and self-skips unless *both*
  DUTs compile the LC3 codec in (`BLE_AUDIO_LC3_SUPPORTED`, i.e. the `esp_audio_codec`
  managed component). It SKIPs on every pair except a dual-audio
  `esp32s31 <-> esp32s31` bench with the codec, where the client encodes a 1 kHz tone
  into LC3 over the CIS and the server decodes it and asserts the tone survived
  (decoded sample count + mean amplitude).
- The control_profiles (7) phase exercises the LE Audio control surface
  (CAP/VCP/MICP/MCP/CCP) and self-skips unless *both* DUTs compile the LE Audio engine
  in (`BLE_AUDIO_SUPPORTED`). The hard gate is the VCP round-trip (client discovers VCS
  + writes volume, server observes the write); CAP discovery is also asserted, and
  MICP/MCP/CCP are exercised and logged.
- The top_profiles (8) phase requires TMAP + GMAP compiled on both DUTs
  (`BLE_AUDIO_TMAP_SUPPORTED` + `BLE_AUDIO_GMAP_SUPPORTED`). TMAP/TMAS is a real
  identity round-trip. GMAP `setRoles()` is called but server GMAS is missing on
  ESP-IDF `release/v6.1` (no host-adapter `gmas.c`); the phase asserts GMAP
  discovery fails and will fail loudly if GMAS ever appears so the Role assert
  can be restored. Media/call/game audio itself still flows through CAP + BAP +
  the control profiles exercised in earlier phases.
- The hearing_aid (9) phase exercises the Hearing Access Service and self-skips unless
  the server compiles the HAS server (`BLE_AUDIO_HAS_SUPPORTED`) and the client compiles
  the HAS client (`BLE_AUDIO_HAS_CLIENT_SUPPORTED`).
