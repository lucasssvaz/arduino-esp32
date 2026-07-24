# LE Audio Layer Design

## Purpose

This document is for maintainers and AI tools working on the LE Audio subsystem
of the BLE library (`src/audio/`). It describes the architecture, the vendor
boundary, the lifecycle contract, and the per-role spec-compliance obligations.
It complements [`DESIGN.md`](DESIGN.md) (the library-wide architecture) — read
that first. Where the two overlap (GATT coexistence, PIMPL, callback model),
this document defers to `DESIGN.md` and only adds audio-specific detail.

## Scope and status

LE Audio is built on Espressif's **ESP-BLE-AUDIO** engine (the Generic Audio
Framework: PACS/ASCS/BAP/CAP and the control/top-level profiles), which is a
**host-agnostic** ESP-IDF component. The whole subsystem is therefore a **fully
shared component** — there are no `.nimble.*` / `.bluedroid.*` files under
`src/audio/`. Both hosts drive the same engine through the same C code.

The packaged Arduino libs track ESP-IDF **`release/v6.1`**. Features that need
engine pieces missing on that line (or on both `v6.1` and `master`) are **stubbed
or scoped down** here — they must not look like working public API. See
[Engine gaps](#engine-gaps-packaged-idf).

### Supported now (feature-guarded)

Each item is behind its own guard; see [Feature guards](#feature-guards).

- **Data plane (BAP):** Unicast Server / Client, Broadcast Source / Sink
  (Scan Delegator / BASS on the sink).
- **Coordination (CAP + CSIP):** CAP Acceptor / Initiator / Commander,
  Coordinated Set Member / Coordinator.
- **Control profiles:** VCP (Renderer / Controller), MICP (Device / Controller),
  MCP via **GMCS only** (Media Player / Controller), CCP via **GTBS only**
  (Call Server / Controller).
- **Top-level identity:** TMAP (TMAS), HAS (device / controller).
- **Turnkey LC3 data plane:** `BLEAudioPlayer` / `BLEAudioRecorder` /
  `BLEAudioPipeline` (ISO ⇄ LC3 ⇄ I2S), when `esp_audio_codec` is present.

### Stubbed / not supported on packaged libs

These APIs may still compile (guards / helpers) but **must not be treated as
on-air complete**. Vendor entry points return `-ENOTSUP` or are documentation-only
helpers:

| Surface | Behavior on packaged `release/v6.1` libs |
| --- | --- |
| **GMAP server** (`createGmap().setRoles()`) | Stubbed: no GMAS (`0x1858`) published. `bleGmapVendorRegister` returns `-ENOTSUP`; `start()` does not fail. Client `discover()` against a peer that exposes GMAS remains available when `CONFIG_BT_GMAP` is on. |
| **Discrete TBS** (non-generic telephone bearer) | Not exposed. CCP is **GTBS-only**. |
| **Discrete MCS** (non-generic media player) | Not exposed. MCP is **GMCS-only**. |
| **OTS (Object Transfer)** | Not exposed in the Arduino API. IDF: draft, NimBLE-only; not on Bluedroid. |
| **PBP in engine-owned BIG adv** | IDF still has no engine PBA-injection hook. The Arduino `BLEAudioBroadcastSource` **does** add PBA (`0x1856`) to the extended adv payload it assembles. |
| **Broadcast Assistant** role | Guard exists; no turnkey `create*()` surface — leave unset until wired. |
| **`audio.end()` → `audio.begin()` after `BLE.end()`** | Not re-enterable: no `esp_ble_audio_common_deinit` / profile-lib deinit (see gaps). Host-interop suite uses a temporary one-phase-per-boot soft-reboot (`BLE_AUDIO_HOST_PHASE_SOFT_REBOOT` / `PHASE_SOFT_REBOOT`) until that lands. |

### Engine gaps (packaged IDF)

Checked against IDF **`release/v6.1`** (Arduino libs) and **`master`**.

**Missing on both branches**

| Gap | Impact |
| --- | --- |
| `esp_ble_audio_common_deinit` (+ profile `AlreadyInit` teardown) | Second `common_init` in one boot fails after `BLE.end()`/`begin()`. |
| Public `esp_ble_iso_common_deinit` | No documented symmetric ISO teardown API. |
| OTS on Bluedroid | Full MCP object transfer unavailable on Bluedroid. |
| Engine PBA injection into BIG adv | Arduino-side `BLEAudioBroadcastSource` adds PBA (`0x1856`) itself; no IDF hook. |

**Missing on `release/v6.1` only (present on `master`)**

| Gap | Impact |
| --- | --- |
| Host-adapter `gmas.c` (NimBLE + Bluedroid) | No GMAS GATT; GMAP server stubbed above. |
| Discrete TBS adapter (`bt_le_tbs_init` / `bt_le_nimble_tbs_*`) | CCP stays GTBS-only on packaged libs. |
| Discrete MCS adapter (`bt_le_nimble_mcs_init`) | MCP stays GMCS-only on packaged libs. |

**Architectural limits (not missing symbols, but constrain designs)**

- One **PACS** registration per device (unicast server vs broadcast sink need separate commits / phases).
- GATT table **frozen** after `common_start` (no role-set swap without host teardown).
- LE Audio APIs are still marked **preview** in IDF.

## Layering

```
sketch
  │  <BLE.h>  (BLEAudio.h is the audio aggregator, pulled in by the umbrella)
  ▼
BLEAudio            controller handle  (src/audio/BLEAudio.{h,cpp}, BLEAudioImpl.h)
  │  create*() factories mint …
  ▼
role handles        BLEAudioUnicastServer, …, BLEAudioHearingAidController
  │  value-type PIMPL handles; backend-agnostic; no esp_ble_audio_* types
  ▼
C vendor boundary   BLEAudio*Vendor.{h,c}   (extern "C", the ONLY place that
  │                 talks to esp_ble_audio_* / bt_* engine APIs)
  ▼
ESP-BLE-AUDIO engine  (ESP-IDF component; host-agnostic)
```

Two hard rules make this stay clean, both enforced by
`tests/check_backend_isolation.sh`:

1. **Public/handle headers are backend-agnostic.** No `esp_ble_audio_*`,
   `bt_*`, or `<zephyr/...>` types may appear in any `src/audio/*.h` that a
   sketch can reach through `<BLE.h>`. Engine types live only in the `*Vendor.c`
   translation units (and their `*Vendor.h`, which are C-only and not part of
   the public umbrella).
2. **The engine boundary is C.** Every `esp_ble_audio_*` call is made from a
   `*Vendor.c` file compiled as C, behind a narrow `extern "C"` surface declared
   in the matching `*Vendor.h`. The C++ handle layer never includes an engine
   header.

### The vendor boundary pattern

For a role `Foo`, the files are:

```
audio/BLEAudioFoo.h        public handle (C++), backend-agnostic
audio/BLEAudioFoo.cpp      handle methods + factory (BLEAudio::createFoo) + dispatch trampolines
audio/BLEAudioFooVendor.h  extern "C" boundary: register/discover/control fns + C callback typedefs
audio/BLEAudioFooVendor.c  the ONLY code that calls esp_ble_audio_foo_*; stubs when the guard is off
```

Each `*Vendor.c` must:

- Wrap its engine calls in `#if BLE_AUDIO_FOO_SUPPORTED` and provide **stub
  definitions** (return an error / no-op) in the `#else` branch, so the whole
  audio component still links on builds where that role's Kconfig is off. This
  is why a sketch can be built against libs that don't yet have a given profile
  compiled in — the guarded handle self-reports "not supported" and the vendor
  stub links.
- Convert engine error codes to `int` (0 == success) at the boundary; the C++
  layer maps that to `BTStatus`.
- Keep the callback typedefs plain-C (`int`, `uint8_t`, raw pointers) — no C++
  types cross the boundary.

## Controller model: accumulate then commit

`BLEAudio` is **not** a second global singleton. It is a shared handle minted
from the `BLE` singleton via `BLE.getAudioController()` — the same
singleton-getter model as `BLE.getScan()`. Its implementation is defined once in
`BLEAudioImpl.h` (`BLEAudioImplCommon` + a neutral `BLEAudio::Impl` combiner),
compiled into every build.

The key mechanism is the **`roleApplies` vector** on the controller Impl:

```cpp
struct BLEAudioImplCommon {
  uint32_t presentationDelayUs = 40000;
  bool active = false;
  std::vector<std::function<BTStatus()>> roleApplies;  // staged role registrations
};
```

- A `create*()` factory allocates the role's shared `Impl`, registers its C
  dispatch callback with the vendor, and **pushes an "apply" lambda** onto
  `roleApplies` (capturing a `std::weak_ptr` to the role Impl so a dropped
  handle registers nothing).
- `BLEAudio::start()` runs every staged apply **in order** — each performs its
  engine registration (PACS/ASCS/CAS/CSIS/VCS/… or a client cb registration) —
  then triggers the **single coordinated `ble_gatts_start()`** through the core
  GATT coordinator. It clears `roleApplies` afterward so a later `start()` does
  not re-apply.

This "stage between `begin()` and `start()`, then commit once" model is what
lets many audio profiles (and a coexisting classic `BLEServer`) land in **one**
committed GATT table. See `DESIGN.md` → "Service staging and startup" for the
coordinator's audio mode.

## Lifecycle and ordering contract

```cpp
BLE.begin("Headset");                     // 1. host stack up
BLEAudio audio = BLE.getAudioController();
audio.setPresentationDelay(40000);        // 2. global defaults (before begin)
audio.begin();                            // 3. engine common_init + event bridge

BLEAudioCapAcceptor cap = audio.createCapAcceptor();   // 4. mint + configure roles
cap.setSetSize(2).setRank(1);
BLEAudioUnicastServer us = audio.createUnicastServer();
us.enableSink(true);
// (optionally) create/stage classic BLEServer services here too

audio.start();                            // 5. run roleApplies, single GATT commit
BLE.getAdvertising().start();             // 6. advertise
// ... runtime ...
audio.end();                              // 7. engine teardown (host stays up)
BLE.end();                                // 8. host teardown
```

Invariants:

- `audio.end()` is best-effort (`bt_le_host_deinit` + role bookkeeping). It does
  **not** fully reset profile `AlreadyInit` state; do not expect
  `BLE.end()` → `BLE.begin()` → `audio.begin()` to succeed in the same boot
  until IDF ships `common_deinit` (see [Engine gaps](#engine-gaps-packaged-idf)).
- `create*()` before `begin()` returns a null handle; role config setters before
  `start()` are the only time server-side capabilities can be set.
- Client-side roles (`*Controller`, `*Initiator`, `UnicastClient`,
  `HearingAidController`, …) still need `begin()`/`start()` to register their
  callbacks with the engine, but only act after an ACL connection + `discover()`.
- A role handle that goes out of scope before `start()` is silently skipped (its
  `roleApplies` lambda locks a dead `weak_ptr`). This is intentional.

## Callback dispatch

Because the engine callbacks are plain C function pointers with no user
`void* arg`, each role uses a **single-link dispatch registry**: a file-scope
`std::weak_ptr<Role::Impl>` (`s_foo`) set by the factory, and a C-linkage
trampoline registered with the vendor. The trampoline locks the weak_ptr and
invokes the user's `std::function`. This mirrors the pattern used across the
control profiles and top-level profiles (e.g. `BLEAudioProfiles.cpp`,
`BLEAudioHearingAid.cpp`).

Consequences and rules:

- **One live role instance per type at a time.** The registry holds one
  weak_ptr; minting a second handle of the same role replaces the dispatch
  target. This matches the engine, which also registers one callback set per
  profile.
- Trampolines must tolerate a dead weak_ptr (role handle dropped) and simply
  return.
- Everything else follows the library-wide callback rules in `DESIGN.md`
  (short, stack-thread execution, no blocking).

## Feature guards

All guards are defined in `core/BLEGuards.h` and are only ever `1` when
`BLE_AUDIO_SUPPORTED` is `1`. Each maps 1:1 to an engine Kconfig symbol so a
handle/factory/example self-excludes when its role is not built. Check the
**specific** guard, never the broad `BLE_AUDIO_SUPPORTED`, when gating a role.

| Guard | Kconfig | Role |
| --- | --- | --- |
| `BLE_AUDIO_SUPPORTED` | `CONFIG_BT_AUDIO` (+ `BLE_ISO_SUPPORTED`) | engine present |
| `BLE_AUDIO_UNICAST_SERVER_SUPPORTED` | `CONFIG_BT_BAP_UNICAST_SERVER` | Unicast Server |
| `BLE_AUDIO_UNICAST_CLIENT_SUPPORTED` | `CONFIG_BT_BAP_UNICAST_CLIENT` | Unicast Client |
| `BLE_AUDIO_BROADCAST_SOURCE_SUPPORTED` | `CONFIG_BT_BAP_BROADCAST_SOURCE` | Broadcast Source |
| `BLE_AUDIO_BROADCAST_SINK_SUPPORTED` | `CONFIG_BT_BAP_BROADCAST_SINK` | Broadcast Sink |
| `BLE_AUDIO_SCAN_DELEGATOR_SUPPORTED` | `CONFIG_BT_BAP_SCAN_DELEGATOR` | BASS (on sink) |
| `BLE_AUDIO_BROADCAST_ASSISTANT_SUPPORTED` | `CONFIG_BT_BAP_BROADCAST_ASSISTANT` | Broadcast Assistant |
| `BLE_AUDIO_CAP_ACCEPTOR_SUPPORTED` | `CONFIG_BT_CAP_ACCEPTOR_SET_MEMBER` | CAP Acceptor (CAS+CSIS) |
| `BLE_AUDIO_CAP_INITIATOR_SUPPORTED` | `CONFIG_BT_CAP_INITIATOR` | CAP Initiator |
| `BLE_AUDIO_CAP_COMMANDER_SUPPORTED` | `CONFIG_BT_CAP_COMMANDER` | CAP Commander |
| `BLE_AUDIO_CSIP_MEMBER_SUPPORTED` | `CONFIG_BT_CSIP_SET_MEMBER` | CSIP Set Member |
| `BLE_AUDIO_CSIP_COORDINATOR_SUPPORTED` | `CONFIG_BT_CSIP_SET_COORDINATOR` | CSIP Set Coordinator |
| `BLE_AUDIO_VCP_RENDERER_SUPPORTED` | `CONFIG_BT_VCP_VOL_REND` | VCP Renderer |
| `BLE_AUDIO_VCP_CONTROLLER_SUPPORTED` | `CONFIG_BT_VCP_VOL_CTLR` | VCP Controller |
| `BLE_AUDIO_MICP_DEVICE_SUPPORTED` | `CONFIG_BT_MICP_MIC_DEV` | MICP Device |
| `BLE_AUDIO_MICP_CONTROLLER_SUPPORTED` | `CONFIG_BT_MICP_MIC_CTLR` | MICP Controller |
| `BLE_AUDIO_MCP_SERVER_SUPPORTED` | `CONFIG_BT_MCS` | MCP Media Player |
| `BLE_AUDIO_MCP_CLIENT_SUPPORTED` | `CONFIG_BT_MCC` | MCP Media Controller |
| `BLE_AUDIO_CCP_SERVER_SUPPORTED` | `CONFIG_BT_CCP_CALL_CONTROL_SERVER` | CCP Call Server |
| `BLE_AUDIO_CCP_CLIENT_SUPPORTED` | `CONFIG_BT_CCP_CALL_CONTROL_CLIENT` | CCP Call Controller |
| `BLE_AUDIO_TMAP_SUPPORTED` | `CONFIG_BT_TMAP` | TMAP identity |
| `BLE_AUDIO_GMAP_SUPPORTED` | `CONFIG_BT_GMAP` | GMAP identity |
| `BLE_AUDIO_PBP_SUPPORTED` | `CONFIG_BT_PBP` | PBP announcement helper |
| `BLE_AUDIO_HAS_SUPPORTED` | `CONFIG_BT_HAS` | HAS device (server) |
| `BLE_AUDIO_HAS_CLIENT_SUPPORTED` | `CONFIG_BT_HAS_CLIENT` | HAS controller (client) |
| `BLE_AUDIO_LC3_SUPPORTED` | `esp_audio_codec` header presence | turnkey LC3 data plane |

> **Guard-vs-linkable-surface note.** `BLE_AUDIO_CAP_ACCEPTOR_SUPPORTED` keys on
> `CONFIG_BT_CAP_ACCEPTOR_SET_MEMBER`, not `CONFIG_BT_CAP_ACCEPTOR`: the engine
> only compiles `esp_ble_audio_cap_acceptor_register` (which instantiates
> CAS + an included CSIS) when the acceptor is also a set member. Always guard on
> the symbol that gates the actual linkable engine entry point, not the
> "conceptual" one — verify against the engine source when adding a role.

## Per-role spec-compliance checklist

When adding or reviewing a role, confirm the mandatory service/announcement/
preset obligations. Non-exhaustive, but these are the ones that bite:

- **CAP Acceptor** must publish the **Common Audio Service (CAS)** with an
  **included CSIS** — a bare PACS/ASCS server is a BAP unicast server but *not*
  a CAP acceptor. This is why the acceptor guard requires the set-member symbol.
- **Unicast Server** must expose **PACS** (published sink/source capabilities +
  available/supported contexts) and **ASCS** (one ASE per stream direction).
  Advertise the correct **Available Audio Contexts** or centrals won't route.
- **Broadcast Source** must emit the **Broadcast Audio Announcement** (0x1852)
  in the extended adv and the **BASE** in periodic adv. The Arduino turnkey
  source assembles that payload (plus a PBA `0x1856` and Broadcast Name `0x30`
  so phone Auracast UIs can find and title it) and binds the BIG to the adv handle.
- **Broadcast Sink** must publish **PACS** (sink caps) and, to be remotely
  assisted, the **Scan Delegator / BASS**. Auto-sync still works without an
  assistant.
- **VCP Renderer** must publish **VCS** (and any compiled VOCS/AICS instances);
  **MICP Device** must publish **MICS** (+ AICS).
- **CSIP Set Member** must expose **CSIS** with SIRK / Set Size / Rank, and (for
  discoverability) the **RSI** in advertising.
- **TMAP** publishes the **TMAS** identity service with a role bitmask; the
  media/call audio still flows through CAP + BAP + the control profiles. A pure
  client sets no local roles and registers nothing.
- **GMAP server publish is stubbed** on packaged libs (no `gmas.c`). Prefer not
  calling `setRoles()` until libs track an IDF with the host adapter; if called,
  register is a no-op stub (`-ENOTSUP`) and no GMAS appears. Client `discover()`
  against a peer that exposes GMAS still works when GMAP is compiled in.
- **CCP** is **GTBS-only** (no discrete TBS API). **MCP** is **GMCS-only** (no
  discrete MCS / OTS Arduino API).
- **HAS device** must register a non-empty **preset list** (needs
  `CONFIG_BT_HAS_PRESET_COUNT > 0`) and set an active preset; the client only
  discovers the Active Index + Control Point when its `preset_switch` callback is
  set.
- **PBP** (`BLEAudioPublicBroadcast`) builds/parses PBA service data. The
  turnkey `BLEAudioBroadcastSource` embeds a PBA (`0x1856`, SQ or HQ matching
  the preset, plus Program Info / Media context) and a Broadcast Name AD
  (`0x30`) in extended adv so independent Auracast UIs (e.g. Galaxy S23/S24)
  can discover and title it. Complete Local Name (`0x09`) alone shows as
  "Unknown" on those UIs.

## GATT coexistence

An ordinary `BLEServer` custom service and the audio profiles live in **one**
committed GATT table. The audio engine performs the service inits and the single
`ble_gatts_start`; the classic server only *stages* its services. The required
ordering is `BLE.begin()` → `audio.begin()` → create/stage server services →
`audio.start()`. This is a NimBLE-side concern (Bluedroid's incremental
multi-app GATTS registration already allows coexistence). The full contract —
including the `BLEGattDatabase` coordinator's standalone vs. audio mode and why
the dependency points one way (NimBLE coordinator observes the host-agnostic
engine) — is documented in `DESIGN.md` → "Service staging and startup".

## Testing

Audio behavior is exercised by:

- Dual-DUT: `tests/validation/ble_audio/` (server + client).
- Host-interop: `tests/validation/ble_audio_host/` (ESP DUT + Linux BlueZ/bleak).

Phases self-skip when a role or host tool is unavailable. Notable pinned gaps:

- `top_profiles` / host `control_gatt.gmap`: **GMAS absence** (GMAP server stub).
- CCP phases use **GTBS** only; MCP phases use **GMCS** only.

When a role's on-air behavior changes (especially when packaged libs pick up
`master`'s `gmas.c` / discrete TBS/MCS), update the suite and drop the matching
stub — the suites are the executable contract.

## Maintainer rules (audio-specific)

In addition to the library-wide rules in `DESIGN.md`:

1. **Never leak engine types across the C++ boundary.** `esp_ble_audio_*` /
   `bt_*` / `<zephyr/...>` stay inside `*Vendor.c`.
2. **Always provide vendor stubs** in the `#else` of each role guard so the
   component links on builds without that Kconfig.
3. **Stage in `roleApplies`, commit in `start()`.** Do not call
   `ble_gatts_start` (or an equivalent per-role commit) from a factory — it
   breaks single-commit coexistence.
4. **Guard on the linkable engine symbol**, not the conceptual profile name
   (see the CAP acceptor note).
5. **Keep it fully shared.** Do not add `.nimble.*` / `.bluedroid.*` files under
   `src/audio/`; the engine is host-agnostic and the subsystem must stay so.
6. **Match new Kconfig in the lib-builder defconfig.** A new role needs its
   `CONFIG_BT_*` symbol in the target defconfig (e.g.
   `esp32-arduino-lib-builder/configs/defconfig.esp32s31`) before the real
   (non-stub) path can link.
7. **Treat the validation suite as part of the design** — add a self-skipping
   phase for any new on-air behavior.

## Reference material

- ESP-BLE-AUDIO component: ESP-IDF `components/bt/esp_ble_audio`
- Bluetooth LE Audio specifications (GAF, BAP, CAP, CSIP, VCP, MICP, MCP, CCP,
  TMAP, GMAP, HAS, PBP): <https://www.bluetooth.com/specifications/specs/>
- Library-wide architecture: [`DESIGN.md`](DESIGN.md)
