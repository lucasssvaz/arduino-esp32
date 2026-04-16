# BLE Library Design

## Status

This BLE library is in the middle of a ground-up refactor for the Arduino ESP32 core.
The API shape, internal architecture, and validation tests in this branch are the current source of truth.
Older `master` behavior is useful only as historical context, not as a compatibility target.

This document is for maintainers and AI tools.
It describes the architecture, invariants, and design rules that should guide future changes.
It intentionally does not describe every file.

## Main goals

The refactor is built around these goals:

- expose one public BLE API for all supported BLE stacks
- use value-style public handles instead of user-managed raw pointers
- standardize result handling on `BTStatus`
- separate GATT properties from security permissions
- support multiple concurrent BLE clients
- keep advanced features such as BLE5, BLEStream, and L2CAP behind the same design rules
- fix long-standing lifecycle, ownership, and spec-compliance issues

## Non-goals

These are **not** current design goals:

- source compatibility with the legacy Kolban-style BLE API
- ArduinoBLE API compatibility in the core library itself
- exposing NimBLE- or Bluedroid-specific types in user-facing APIs
- letting public behavior drift per backend

Backend differences may exist internally, but they should stay behind the common API surface.

## High-level architecture

The library is split into three layers:

1. **Public API layer**: `BLE.h` and the public handle types (`BLEServer`, `BLEClient`, `BLEService`, `BLECharacteristic`, `BLEDescriptor`, `BLEScan`, `BLEAdvertising`, `BLESecurity`, remote GATT types, `BLEStream`, `BLEL2CAP*`).
2. **Backend-selection layer**: `impl/BLEGuards.h` plus thin backend switch headers such as `BLEServerBackend.h`, `BLEClientBackend.h`, `BLEScanBackend.h`, and related files.
3. **Concrete backend layer**: `impl/nimble/*` and `impl/bluedroid/*`.

The public layer owns the API contract.
Backends implement that contract and should not redefine it.

## Backend model

Backend choice is centralized in `impl/BLEGuards.h`.
All BLE code is expected to use these guards instead of repeating raw SDK config checks.

Current guard model:

- `BLE_ENABLED`: any supported BLE backend is available
- `BLE_NIMBLE`: NimBLE backend selected
- `BLE_BLUEDROID`: Bluedroid backend selected
- `BLE5_SUPPORTED`: BLE 5 features available on the active backend/target
- `BLE_L2CAP_SUPPORTED`: L2CAP CoC available; currently NimBLE-only and config-dependent

### Design rule

Do not push backend-specific conditionals into user code or public APIs unless there is no alternative.
If a feature is stack-exclusive, prefer keeping the public API and returning `BTStatus::NotSupported` at runtime with a log message.

## Public object model

### `BLE`

`BLE` is the global entry point.
It owns stack initialization, shutdown, controller-level settings, and factories for the rest of the library.

### Singleton-style handles

These objects are effectively global shared handles returned by `BLE`:

- `BLE.createServer()`
- `BLE.getScan()`
- `BLE.getAdvertising()`
- `BLE.getSecurity()`

Today, those factories are backed by lazily created static shared implementations.
Repeated calls return handles to the same underlying object.

### Per-instance handles

`BLE.createClient()` returns a fresh client instance each time.
This is a hard design requirement of the refactor: multi-client support must remain possible.

### GATT handles

Server-side and client-side GATT objects are lightweight handles:

- local: `BLEService`, `BLECharacteristic`, `BLEDescriptor`
- remote: `BLERemoteService`, `BLERemoteCharacteristic`, `BLERemoteDescriptor`

These objects are copied by value but refer to shared underlying state.

### Connection metadata

`BLEConnInfo` is the stack-agnostic connection descriptor.
It replaces backend-specific callback parameter types and stores its data inline.
Public APIs should prefer `BLEConnInfo` over raw NimBLE/Bluedroid structs.

## Ownership and lifetime rules

The public library model is handle-based.
Most public BLE types wrap `std::shared_ptr<Impl>` and are cheap to copy.

Important consequences:

- copying a handle shares ownership of the same underlying BLE resource
- default-constructed handles are null/invalid
- public methods must tolerate null handles and fail safely
- users must never `delete` BLE objects obtained from the public API

The null-handle guard pattern is standardized through `BLE_CHECK_IMPL(...)`.
If a public handle has no implementation, methods return a safe default or `BTStatus::InvalidState`.

### Design rule

New public BLE handle types should follow the same model unless there is a strong, explicit reason not to.
Do not introduce new user-owned heap objects for normal BLE flows.

## Callback model

The preferred callback model is per-event registration on each object, usually via `std::function`.
Examples include:

- `BLEServer::onConnect(...)`
- `BLEClient::onDisconnect(...)`
- `BLECharacteristic::onWrite(...)`
- `BLEScan::onResult(...)`
- `BLERemoteCharacteristic::subscribe(..., callback)`

There is still some transitional compatibility surface:

- several classes still expose `Callbacks` base classes plus `setCallbacks(...)`
- `BLESecurity` currently uses function-pointer callback types
- `BLEAdvertising::onComplete(...)` currently uses a function pointer

So the design direction is clear, but the refactor is not fully uniform yet.
When extending the library, prefer the per-event registration style and avoid adding new abstract callback class requirements.

### Callback dispatch rule

Callbacks are copied under the object mutex and invoked after the lock is released.
This avoids calling user code while internal locks are held.
Preserve that pattern.

### Execution-context rule

BLE callbacks can run on stack-controlled contexts, not only on the Arduino `loop()` thread.
They must stay short, avoid blocking the stack, and avoid assumptions about call ordering relative to sketch code.
The validation tests explicitly guard against callback/host ordering races.

## Concurrency and synchronization

This library uses FreeRTOS primitives directly.
It deliberately avoids `std::mutex` to keep binary size down.

Current primitives:

- `BLELockGuard`: RAII wrapper around a FreeRTOS recursive mutex
- `BLESync`: blocking bridge for async stack operations

`BLESync` is used when the public API offers a blocking operation while the backend completes it asynchronously.
The expected pattern is:

1. prepare with `take()`
2. start async stack work
3. wait with `wait(timeoutMs)`
4. release from the stack callback with `give(status)`

### Design rules

- use `SemaphoreHandle_t` + `BLELockGuard` for internal BLE mutexes
- do not introduce `std::mutex` into BLE internals
- when wrapping async backend flows with synchronous public APIs, use `BLESync` instead of task notifications or ad-hoc synchronization
- never invoke user callbacks while holding an internal mutex

## Error model

`BTStatus` is the standard status/result type for BLE operations.
It is shared with BT Classic infrastructure and is intentionally tiny.

### Design rules

- return `BTStatus` for operations that can fail
- use specific statuses (`Timeout`, `NotFound`, `NotSupported`, `AlreadyConnected`, `InvalidState`, etc.) instead of generic failure where possible
- do not mix `bool`, raw stack error codes, and silent failure semantics in new public BLE APIs
- convert backend-native errors into `BTStatus` at the boundary

## GATT model and rules

### Properties vs permissions

The refactor treats these as separate concepts:

- `BLEProperty`: what a characteristic can do
- `BLEPermission`: what security is required to access it

This separation is fundamental and must be preserved.
Do not reintroduce stack-specific property/permission conflation into public APIs.

### Service staging and startup

Creating services and characteristics builds up an in-memory GATT model.
`BLEServer::start()` is the registration point that pushes that model into the backend stack.
Code should not assume that `createService()` alone means the service already exists in the controller.

### Descriptor rules

- CCCD is auto-created for notify/indicate characteristics
- standard descriptor helpers are exposed through `BLEDescriptor`
- descriptor behavior should remain stack-agnostic from the caller's perspective

### Service removal

`BLEServer::removeService()` has backend-specific implementation details:

- NimBLE rebuilds the full GATT database
- Bluedroid deletes the service through the stack

The public semantic is still one API.
Any new code must preserve this abstraction and document behavior differences only when they materially affect runtime expectations.

## Lifecycle rules

`BLE.begin()` initializes the selected backend and `BLE.end()` shuts it down.
The BLE singleton tracks whether the stack is initialized and whether controller memory was permanently released.

Important lifecycle invariants:

- most BLE work is invalid before `BLE.begin()`
- `BLE.end(true)` is terminal for the process lifetime in the current design; reinitialization is blocked after controller memory release
- factories return invalid handles when BLE is not initialized
- shutdown must leave the library in a safe, non-dangling state even if public handles still exist

### Design rule

Changes to lifecycle code must be validated against memory-release and reinitialization behavior.
This is a critical contract for sketches that switch between radio features at runtime.

## Stack-agnostic API rules

The public API should describe BLE concepts, not backend implementation details.
That means:

- use `BTAddress`, not backend address structs
- use `BLEConnInfo`, not backend callback params
- keep backend names out of normal user-facing method signatures
- keep feature parity where practical
- if parity is impossible, fail predictably with `BTStatus::NotSupported`

This rule applies even when one backend requires substantially more internal plumbing than the other.

## Feature-specific notes

### BLE5

Extended advertising, periodic advertising, extended scanning, periodic sync, PHY control, and data length control all belong to the common API surface when supported.
Gate backend availability through `BLE5_SUPPORTED` and runtime status reporting, not by fragmenting the public interface.

### BLEStream

`BLEStream` is a higher-level convenience layer that exposes the Nordic UART Service through the Arduino `Stream` API.
It should follow the same stack-agnostic and lifecycle rules as the lower-level BLE objects.

### L2CAP CoC

L2CAP CoC is currently guarded by `BLE_L2CAP_SUPPORTED` and only available when the backend/config supports it.
This is an accepted exception because capability depends on both backend and build configuration.
Even so, the public API remains generic and should not expose backend types.

## Testing as executable design

The BLE validation suite in `tests/validation/ble/` is the best executable summary of the intended behavior.
It currently exercises:

- basic init/deinit lifecycle
- BLE5 extended and periodic advertising/scanning
- GATT server/client setup and discovery
- read/write flows
- notifications and indications
- large ATT writes
- descriptor read/write behavior
- write without response
- server-initiated disconnect
- pairing/security flow
- BLE5 PHY and data length updates
- reconnect cycles
- `BLEStream`
- L2CAP CoC when supported
- memory release after `BLE.end(true)`

### Design rule

When behavior changes, update the validation tests or add new ones so the intended contract stays executable.
For cross-stack changes, think in terms of common behavior first and backend mechanics second.

## Maintainer rules

When changing this library, keep these rules in mind:

1. **Protect the common API surface.** Public BLE APIs should not require NimBLE/Bluedroid conditionals in sketches.
2. **Prefer handle semantics.** Public objects should remain cheap, copyable handles with shared ownership.
3. **Use `BTStatus`.** New failure paths should be explicit and structured.
4. **Preserve property/permission separation.** Do not collapse them back together.
5. **Keep callbacks safe.** Copy callback state under lock, invoke outside the lock, and assume stack-thread execution.
6. **Use the existing synchronization primitives.** `BLELockGuard` and `BLESync` are the standard internal tools.
7. **Keep lifecycle strict.** Initialization, shutdown, and memory release semantics are core library contracts.
8. **Treat tests as part of the design.** If the intended behavior changes, the validation suite should change with it.
9. **Document current reality, not old behavior.** Legacy `master` behavior is reference material, not the default answer.

## Practical guidance for AI tools

If you are modifying this library:

- start from the public contract in the headers and the validation tests
- check whether the change affects both backends
- prefer adapting backend code to the shared API rather than changing the shared API to fit one backend
- look for existing patterns in handle ownership, callback dispatch, and `BTStatus` conversion before inventing a new one
- treat `MIGRATION.md` as a user-facing transition guide and this document as the maintainer-facing architecture guide

If the code and this document disagree, follow the code and update this document.
