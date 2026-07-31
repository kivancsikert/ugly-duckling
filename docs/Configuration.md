# Configuration

How a device's configuration is stored, applied, and reconciled with the server. This document
describes the current firmware implementation. For the full protocol design and rationale (server
side, wire formats, phased rollout), see
[`specs/config-reconciliation.md`](specs/config-reconciliation.md) — that document also tracks what
is and isn't built yet via its progress checklist; this one describes what's actually running.

## Concepts

- **Device configuration** and **function configuration** are each a **reconciled configuration**:
  a JSON body the server authored, identified by an opaque **fingerprint** token. The firmware
  never computes or reinterprets a fingerprint — it only stores and echoes back whatever the server
  sent, so a serialization difference between server and firmware can never cause the two to
  disagree about whether a configuration has changed.
- Each configuration is persisted as an **envelope**: `{data, fingerprint, requestedAt}`, with
  `data` kept byte-for-byte verbatim. See [`ConfigEnvelope`](../components/kernel/src/ConfigEnvelope.hpp)
  / [`StoredConfig`](../components/kernel/src/StoredConfig.hpp).
- The device holds up to two full configuration sets (device configuration + every function) at
  once, in interchangeable **slots** named `a`/`b`. A small **`config-state`** record says which
  slot is **`confirmed`** (last-known-good, what the device actually boots and runs) and, while a
  new set is being tried, which slot is **`requested`** and how far it's gotten. See
  [`ConfigState`](../components/kernel/src/ConfigState.hpp) /
  [`ConfigStateStore`](../components/kernel/src/ConfigStateStore.hpp).
- Applying `confirmed` at boot is **best-effort**: a peripheral or function that fails to apply is
  reported as an error, but the device still boots and runs. Applying a `requested` set is
  **strict**: any failure reverts to `confirmed` and reboots. See
  [`decideBootPlan`/`recordStrictBootOutcome`](../components/kernel/src/ConfigBootPlan.hpp).
- Three MQTT messages carry all of this: the device announces itself on **`BOOT`**, advertises what
  it's actually running on **`SYNC`**, and receives new configuration on **`UPDATE`**.

## BOOT, SYNC, UPDATE

All three sit directly under the device's MQTT root (`.../devices/ugly-duckling/$INSTANCE`),
alongside but independent of `commands`/`responses`.

| Topic | Direction | Retention / QoS | Carries |
| --- | --- | --- | --- |
| `boot` | device → server | `NoRetain`, `QoS 1` | Diagnostics: model/revision/platform, reset/wakeup reason, boot count, per-peripheral/function apply errors, and (see *Rejection reporting* below) a rejection code, if one is pending. **No configuration bodies.** |
| `sync` | device → server | `NoRetain`, `QoS 2` | The fingerprint manifest of what the device has **applied and booted with** — proof-of-apply, not proof-of-receipt. Built from live in-memory state, never re-derived from NVS. Also carries a rejection code (see *Rejection reporting* below) on the first `SYNC` published after a revert, alongside `BOOT`. |
| `update` | server → device | `NoRetain`, `QoS 2` | New configuration: `{configurations: {device: envelope, <function>: envelope, ...}}`. |

- **`BOOT`** is published once per boot, from [`startDevice()`](../components/devices/src/Device.hpp)
  (`mqttRoot->publish("boot", ...)`), unconditionally as soon as peripherals/functions finish
  initializing.
- **`SYNC`** is published by a dedicated task
  ([`initSyncTask`](../components/devices/src/Device.hpp)) triggered by a single-element overwrite
  queue: every successful MQTT (re)connection, and immediately after a successful hot-reload
  `UPDATE`. It always waits for `kernelReady` first, so it only ever reports a fully-booted set.
  There is no boot-time `SYNC` separate from the connection trigger.
- **`UPDATE`** is handled by [`registerUpdateHandler`](../components/devices/src/Device.hpp): it
  filters the incoming configurations against currently-held fingerprints (an entry whose
  fingerprint already matches is dropped; if nothing differs, the whole message is a no-op — see
  [`filterUpdate`](../components/kernel/src/UpdateFilter.hpp)), then branches:
  - **Device configuration changed** → persist the changed envelopes and reboot. The boot sequence
    re-derives everything, since a device change can restructure which functions exist.
  - **Only function configuration(s) changed** → hot-reload each live, via
    [`FunctionRegistry::reconfigure`](../components/functions/src/functions/Function.hpp), and
    trigger a `SYNC`.

  Function configuration used to arrive on a retained `functions/$NAME/config` topic and apply
  live on receipt; that subscription is gone. `UPDATE` is the only config-in path.

## Storage: envelopes and slots

Every configuration is a `StoredConfig`-backed envelope, keyed by name (`device`, or a function
name) within an `NvsStore` namespace. `StoredConfig` never parses `data` into a typed object — it's
opaque bytes as far as storage is concerned; parsing is a separate step done by the caller.

Two namespace layouts coexist today:

- **Flat (Phase 1, still what every real device uses):** `config`/`device-config` for the device
  document, `function-cfg` for functions — one envelope per key, no slot concept.
- **Slotted (machinery in place, not yet driving real devices — see *Current implementation
  status*):** `config-a`/`config-b` (key `device`) and `function-cfg-a`/`function-cfg-b`, mirroring
  each other so a slot is fully self-contained. A separate `config-state` namespace (key `state`)
  holds the `ConfigState` record described above.

A `StoredConfig` also recognizes a **legacy bare body** — a blob written directly under the key by
pre-reconciliation firmware, no `{data, fingerprint, requestedAt}` wrapper — and adopts it with an
**empty fingerprint** (which can never match a real one, so the next `UPDATE` for that name always
applies) rather than treating it as a parse failure.

## The confirmed/requested state machine

```mermaid
flowchart TD
    Start(["Boot"]) --> ReadState["Read config-state"]
    ReadState --> HasRequested{"requested set?"}

    HasRequested -- "no" --> LoadConfirmed["Load confirmed slot<br>(or nothing), best-effort"]

    HasRequested -- "pending" --> MarkAttempted["Persist: pending → attempted"]
    MarkAttempted --> LoadStrict["Load requested slot, strictly"]
    LoadStrict --> Applied{"applied<br>cleanly?"}
    Applied -- "yes" --> Commit["Commit:<br>confirmed = slot, requested cleared"]
    Applied -- "no" --> Reject["Mark rejected,<br>record rejection code"]
    Reject --> Reboot(["esp_restart()"])

    HasRequested -- "attempted (crash) or rejected" --> Revert["Revert:<br>drop requested,<br>record rejection if not already set"]
    Revert --> LoadConfirmed

    Commit --> Continue["Continue boot:<br>BOOT, kernelReady, SYNC"]
    LoadConfirmed --> Continue
```

- **`confirmed`** — which slot (if any) is last-known-good. Absent means "no confirmed slot": the
  device boots with defaults, no functions, exactly like an empty NVS slot.
- **`requested`** — `{slot, status}` for a staged-but-unconfirmed set, or absent.
  `status ∈ {pending, attempted, rejected}`.
- **`rejection`** — an optional code left over from a failed attempt, cleared once reported (see
  below).

`decideBootPlan()` ([`ConfigBootPlan.hpp`](../components/kernel/src/ConfigBootPlan.hpp)) is a pure
function of `ConfigState` implementing the diagram above; `recordStrictBootOutcome()` implements the
commit/reject step after a strict load is attempted. Both are unit-tested directly
(`ConfigBootPlanTest.cpp`) independent of NVS, so every state transition — including "crashed while
attempted" — has a table-driven test with no real boot required.

**Commit is a single pointer flip**: `confirmed` is set to the slot that was `requested`, and
`requested` is cleared — one NVS write. A crash between marking `pending → attempted` and this write
is harmless: the next boot sees `attempted` and reverts, exactly as a genuine apply failure would.

## Rejection reporting

When a `requested` set is reverted, a `rejection` code is recorded in `config-state` (defaulting to
`INTERNAL` today — classifying by cause, e.g. `INVALID_ARGUMENT` for a parse failure, is future
work). It's reported **once**, on both channels: the next `BOOT` message includes a `rejection`
field if one is set, and so does that same boot's next `SYNC`, if one is published. `BOOT` carries
it because it fires deterministically on every boot, including a revert boot itself, without
waiting on `kernelReady` or a live MQTT connection the way `SYNC` does; `SYNC` also carries it
because it's the channel most clients are already watching for reconciliation state. The field is
cleared from `config-state` immediately after being read for `BOOT` — matching this system's
existing no-delivery-guarantee posture (an `UPDATE` published while offline is similarly never
re-sent; reconciliation converges through re-push, not delivery guarantees), so a `BOOT` that fails
to reach the server also drops that rejection rather than retrying it. The in-memory copy handed to
the `SYNC` task is consumed after its first publish, so a later `SYNC` in the same boot session
doesn't repeat it.

## Current implementation status

Everything above except the following is live on real devices today:

- **Nothing currently populates a `requested` slot.** `registerUpdateHandler`'s device-changed
  branch still writes straight into the flat, unslotted storage and reboots (Phase 1 behavior,
  unchanged) — it does not yet stage into `requested`. The confirmed/requested state machine, the
  slotted NVS layout, and the strict/revert boot logic are real and boot-wired, but exercised today
  only by tests that seed NVS directly
  ([`ConfigStateStoreTest.cpp`](../test/embedded-tests/components/kernel-test/src/ConfigStateStoreTest.cpp)),
  not by live traffic. This is intentional, incremental sequencing — see
  [`specs/config-reconciliation.md`](specs/config-reconciliation.md)'s Phase 3 checklist for what
  wires it up for real.
- **`SYNC` does not yet include the `device` fingerprint** — only functions.
- **Rejection codes are always `INTERNAL`** — the cause-specific mapping
  (parse/validation → `INVALID_ARGUMENT`, unknown function type → `UNIMPLEMENTED`, NVS-full →
  `RESOURCE_EXHAUSTED`) isn't implemented.
