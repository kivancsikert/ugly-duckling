# Config Reconciliation — firmware side (`BOOT`/`SYNC`/`UPDATE`, fingerprints, confirmed delivery)

> Status: **Not started.** This is the firmware counterpart to the server-side spec in the app repo,
> [`cornucopia-app/docs/specs/config-reconciliation.md`](../../../cornucopia-app/docs/specs/config-reconciliation.md).
> **Read that spec first** — it owns the protocol design, the *why* behind every decision, and the
> server/UX half. This document is the firmware implementation plan.
>
> **Prerequisite:** [`configuration-as-schema.md`](configuration-as-schema.md) — the `Configuration`/`Property`
> teardown. This plan **assumes that refactor is already done**: configuration bodies are stored and echoed as
> **verbatim JSON**, and persistence is separated from parsing. Land that first on its own branch, then build
> this on top.
>
> The one-line summary of the firmware's job: **persist each configuration as `(data, fingerprint, requestedAt)`,
> echo the fingerprints back in a `SYNC` manifest, apply configuration live (function-only change) or across a
> reboot (device change), and report what is actually *running* — never compute a fingerprint, only parrot it.**

## What the firmware is responsible for (and what it is not)

The server is the **authority**. It decides what a device should run, holds `requested`/`confirmed` state,
derives "pending", runs the retry/deadline policy, and owns the UX. The firmware's contract is deliberately
dumb and stateless-by-comparison:

- **Store what it is handed, verbatim.** Each `UPDATE` carries, per configuration, a body plus an opaque
  `fingerprint` and a `requestedAt` timestamp. The firmware persists the three together and **never hashes
  anything** — the fingerprint is a token it echoes, so a serialization mismatch can never cause an
  apply-resend-apply livelock (server spec, *"The identity of a revision"*). Storing the body verbatim is what
  makes this safe (see [`configuration-as-schema.md`](configuration-as-schema.md)).
- **Skip what it already has.** If an `UPDATE` names a configuration whose `fingerprint` the device already
  holds, that entry is a no-op. If **nothing** in an `UPDATE` differs, the whole message is ignored — no
  re-apply, no reboot.
- **Report what is running, not what is held.** `SYNC` is a manifest of the fingerprints of the configurations
  the device has **applied and booted with** — proof-of-apply, not proof-of-receipt. It is built from the
  **`confirmed`** configuration set (see *Storage*), and only sent **after a fully successful boot**.
- **Keep running without the network.** The device applies its last-known-good (`confirmed`) configuration at
  boot regardless of connectivity, and reconciles with the authority only when a connection exists. An
  unreachable server never stalls the device.

Everything about *when to retry*, *what "pending" means*, and *what the user sees* lives on the server.

### New-protocol only — no dual stack, no legacy compatibility

This firmware speaks **exactly one protocol: the new one.** There is no in-firmware compatibility with the old
`init`-at-boot / retained-`config`-topic protocol, and we only ever talk to a server that speaks the new
protocol end-to-end. This removes the "awkward hybrid" sequencing constraint the earlier draft worried about:
the `BOOT`/`SYNC` split, the `UPDATE` channel, and device+function reconciliation all land **together**, in one
self-consistent build. Coordinate the cutover so the server is on the new protocol before the first
new-protocol device connects.

## Naming: `settings` → `device configuration`

As part of this work we **rename what the code currently calls (device) `settings` to "device configuration."**
Today `DeviceSettings` ([`DeviceSettings.hpp`](../../components/devices/src/devices/DeviceSettings.hpp)) holds
the `peripherals`/`functions` lists (which determine *which functions and peripherals exist*) plus device-wide
tunables, loaded from the `device-config` key. Under reconciliation this is just **one more configuration
document** — the `device` configuration — that carries a `fingerprint` like any other. The rename aligns the
code with the protocol vocabulary (`device` vs. per-function configurations) and removes the "settings vs.
config" ambiguity. This is tracked as its own checklist item because it touches type names, log strings, and
the boot path broadly.

## Where the firmware is today (grounding)

Concrete anchors this plan builds on — verify these still hold before starting:

- **`init` is published once per boot** in
  [`Device.hpp` `startDevice()`](../../components/devices/src/Device.hpp). It carries device identity, the full
  device configuration document, diagnostics (`reset`/`wakeup`/`bootCount`/`time`), an `InitState`, the full
  per-peripheral/per-function init JSON (including `params`/`config`/`error`), and a crash report.
  → This splits into **`BOOT`** (diagnostics + per-peripheral/function `error` feedback, **no configuration
  bodies**) and **`SYNC`** (fingerprint manifest).

- **Function configuration arrives on a retained topic and is applied live.**
  [`Function.hpp` `makeFunctionFactory`](../../components/functions/src/functions/Function.hpp) subscribes each
  function to `functions/<name>/config`; on a message it persists then calls `configure(...)`. This retained
  subscription is **removed**; `UPDATE` becomes the only config-in path. The live `configure(...)` call is the
  exact hot-reload path `UPDATE` reuses.

- **Config is persisted as a bare body in NVS**, and `NvsConfiguration` couples parse and persist — both
  addressed by the [`configuration-as-schema.md`](configuration-as-schema.md) prerequisite and the new
  per-configuration store below.

- **Device configuration** is loaded at boot from `device-config`; function configuration lives per-function in
  the `function-cfg` namespace. **Network configuration** (`network-config`: MQTT broker, WiFi, instance) is
  **separate and out of scope** — it is provisioned independently, must survive across everything here, and is
  never part of the reconciled set.

- **`FunctionManager`** ([`Function.hpp`](../../components/functions/src/functions/Function.hpp)) creates
  functions from the device configuration's `functions` list and holds their handles. It becomes the
  **`FunctionRegistry`** — the in-memory source of truth for `name → {live function, fingerprint}` (see below).

- **MQTT topic model.** [`MqttRoot`](../../components/kernel/src/mqtt/MqttRoot.hpp) roots every device at
  `…/devices/ugly-duckling/<instance>` and offers `publish`/`subscribe`/`clear`/`registerCommand`. `boot`,
  `sync`, `update` sit **directly under the device root**, orthogonal to `commands`/`responses`.
  `UPDATE`/`SYNC` deliberately do **not** reuse the command channel (server spec, *"`UPDATE` is not a
  command"*).

- **Connection lifecycle.** [`MqttDriver`](../../components/kernel/src/mqtt/MqttDriver.hpp) forces a **clean
  session on every (re)connect** — the persistent-session path is disabled (`nextSessionShouldBeClean` stays
  true; see the `TODO` around the `Connected` handler). There is **no application-level "connection
  established" callback** today; the `SYNC` trigger needs one (see *SYNC*).
  [`states->kernelReady`](../../components/kernel/src/KernelStatus.hpp) is set at the end of a successful boot
  and is the "boot succeeded" signal the SYNC task gates on.

- **Fingerprints are never computed on-device.** The device stores and echoes the server's token verbatim.

## Storage: verbatim envelopes in swappable `confirmed`/`requested` namespaces

### The per-configuration envelope

Each configuration (the `device` document and each function) is stored as one **verbatim envelope**:

```jsonc
{
  "data": { /* the configuration body, byte-for-byte as the server sent it */ },
  "fingerprint": "…hex…",                // opaque token from the authority; never recomputed
  "requestedAt": "2026-07-30T12:34:56Z"  // authored-at stamp, echoed back; encoded exactly like a
                                         // plot-controller schedule `start` (ISO 8601 string), not interpreted
}
```

A small **per-configuration store wrapper** (`StoredConfig`) owns reading/writing one such envelope in NVS and
exposing `data` (verbatim) + `fingerprint` + `requestedAt`. This is **separate from parsing** — it never
constructs a `Configuration`; parsing the `data` into a typed snapshot is a distinct step
([`configuration-as-schema.md`](configuration-as-schema.md)). `NvsConfiguration`'s old parse-and-persist
coupling is retired in favor of this split. One `nvs_set_blob` per envelope keeps `data`/`fingerprint`/
`requestedAt` inseparable (per-configuration atomicity).

The **device configuration** is held at runtime by its own `StoredConfig` (loaded at boot from the `confirmed`
slot). That in-memory object is the **source of truth for the `device` manifest entry's fingerprint and
`requestedAt`** — the single-document analogue of `FunctionRegistry` (below), which is the source of truth for
every function's fingerprint. The `SYNC` builder reads both, never re-deriving anything from NVS at publish
time.

Because no legitimate configuration body has top-level `data` or `fingerprint` keys, the envelope shape is
unambiguous. There is **no legacy bare-body reader** — this build never reads the old format (see *Migration*).

### Two config-set slots + a state pointer

The reconciled set (the `device` envelope + one envelope per function, keyed by function name) lives in a
**slot namespace**. There are two interchangeable slots — `a` and `b`, say — and a separate small
**`config-state`** namespace that records:

- `confirmed`: which slot (`a`/`b`) holds the last-known-good set. Always present after first provisioning.
- `requested`: `{ slot, state }` for a new-but-unconfirmed set, or absent. `state ∈ { pending, attempted,
  rejected }`.
- `rejection`: an optional stored `google.rpc.Code` to report on the next `SYNC` (Phase 3+; see *Rejection*).

Each slot namespace holds a **full** set (device + every function), so a slot is self-contained: a boot loads
exactly one slot and never merges. Network configuration stays in its own stable namespace and is never
swapped.

> **Phase 1 note.** The `requested` slot + state machine below is the **atomicity mechanism** and lands in
> Phase 3. In Phase 1 there is a single `confirmed` slot and no revert: an `UPDATE` writes through and (if
> device config changed) reboots; a boot that then fails is unrecoverable **exactly as today**. The full
> two-slot design is documented here as the end goal so the Phase 1 shape is chosen to grow into it.

## How reconciliation works (end-goal design)

### Receiving an `UPDATE`

1. **Filter by fingerprint.** For each named configuration (`device` and/or functions), drop entries whose
   `fingerprint` already matches the `confirmed` slot. **If nothing differs, ignore the `UPDATE` entirely.**
2. **Stage into `requested`.** Take the free slot (the non-`confirmed` one), copy every unchanged configuration
   from `confirmed`, and overwrite the changed ones with the new envelopes. Mark `requested = { slot, pending }`.
3. **Apply:**
   - **Device configuration changed → reboot.** Do not hot-reload; let the boot sequence apply the whole
     `requested` set (peripherals ride inside the device document — a device change can restructure which
     functions exist).
   - **Only function configurations changed → hot-reload.** Mark `requested` `attempted`, then for each changed
     function call `FunctionRegistry.reconfigure(name, envelope)` → `configure(...)`. On success of all, **commit**
     (`requested` becomes `confirmed`, old `confirmed` dropped, `requested` cleared). On any failure, mark
     `rejected` and **reboot** (boot reverts to `confirmed`).

In **Phase 1**, steps 2–3 collapse: write the changed envelopes straight into the single `confirmed` slot; if
the device document changed, reboot; otherwise hot-reload the changed functions. No `requested`, no revert.

### Boot

Read `config-state`, then:

- **No `requested`** → load `confirmed`. Boot. If boot fails, there is no recourse — fail as today (best-effort
  try to get logs out over MQTT, but be prepared that MQTT may also be unavailable).
- **`requested` == `pending`** → mark `attempted`, load `requested`. On a detected failure → mark `rejected`
  and reboot (next boot reverts). On full success → **commit** (`requested` → `confirmed`).
- **`requested` == `attempted`** (we started applying but crashed before committing or gracefully rejecting) or
  **`rejected`** → record the rejection for the next `SYNC`, load `confirmed`, drop `requested`.

So a bad `requested` set costs one extra reboot and always lands back on the last-known-good `confirmed` set.
This is the atomic-revert mechanism; it is **Phase 3**. Phase 1 has only the "no `requested` → load
`confirmed`" branch.

**Commit is a single pointer flip.** "Commit" means: flip `config-state.confirmed` to point at the slot that
was `requested` — **one NVS write, and that write is the commit point.** Everything after it (dropping the old
slot, clearing `requested`) is idempotent cleanup that the next boot can re-derive, so a crash mid-cleanup is
harmless: a boot that finds `confirmed` already pointing at the former `requested` slot simply finishes the
cleanup.

**What counts as boot success vs. failure** differs by which set is loaded:

- **Booting the `confirmed` set is best-effort.** A peripheral or function whose apply errors is *recorded*
  (the `InitState`/`error` feedback that rides on `BOOT`, as today), but boot still completes, `kernelReady` is
  set, and `SYNC` is sent. There is no recourse — the last-known-good set is all we have.
- **Booting a `requested` set is strict.** *Any* peripheral/function apply error (or a crash before boot
  completes, i.e. `requested` still `attempted`) is a **detected failure** → mark `rejected`, reboot, revert to
  `confirmed`. Only an error-free boot of a `requested` set commits and lets `SYNC` fire.

### `FunctionRegistry` — the in-memory source of truth

`FunctionManager` becomes `FunctionRegistry`. It owns, per live function, `{ handle, fingerprint }`:

- **Populated at boot** as each function is created from the loaded slot: the registry records the function's
  fingerprint from its envelope once `configure(...)` **succeeds** (proof-of-apply, not proof-of-receipt).
- **`reconfigure(name, envelope)`** — the hot-reload entry point `UPDATE` calls: persist the envelope, parse,
  `configure(...)`, and on success update the stored fingerprint. This is the logic that lives today inside the
  per-function `functions/<name>/config` subscription lambda; it **moves out of the factory into the
  registry**, so there is one apply path shared by boot and `UPDATE`, and one place that knows every function's
  current fingerprint.
- **`manifest()`** — yields `name → fingerprint` for the `SYNC` builder, straight from in-memory state (never
  re-reading NVS, so it reflects what actually applied).

A configuration for a function **not defined in the (potentially updated) device configuration** is a
**protocol violation**, treated with the same seriousness as a malformed body — the server should never send
one. It is a faulty configuration → `INVALID_ARGUMENT` once rejection exists (Phase 3); before then it is
simply unhandled, exactly like any other faulty configuration today. Note the check is against the *post-update*
device configuration: an `UPDATE` that introduces a new function bundles the device-configuration change that
defines it, which reboots and rebuilds the registry, so the function *is* defined by the time its config
applies.

### `SYNC`

- **Trigger: any successful MQTT connection**, delivered through a **single-element overwrite queue** into a
  dedicated SYNC task. Coalescing means a flurry of reconnects (or a post-`UPDATE` request) collapses to one
  pending SYNC — no pile-up if the link is flaky. (Explicit rate-limiting/throttling beyond the overwrite queue
  is deemed unnecessary for now.)
- **Gate: only after a fully successful boot.** The SYNC task awaits `kernelReady` (all functions configured
  successfully) before publishing. If MQTT connects early, SYNC waits. If boot is **unsuccessful** with a
  `requested` set, the device reboots to revert and **never sends SYNC** for that failed attempt.
- **There is no boot-time / `startDevice()` SYNC.** With nothing to sync against absent a connection, the
  connect trigger is the only trigger; the connection-established hook covers first boot and every reconnect
  uniformly.
- **Payload:** `{ configurations: { device: {fingerprint, requestedAt}, <function>: {fingerprint, requestedAt},
  … } }` built from the `confirmed` slot / registry. A stored `rejection` code (Phase 3+) is included, then
  **cleared locally** so it is reported exactly once. `NoRetain, QoS 2`.

The **connection-established hook** is new surface on `MqttDriver`: a registered callback (analogous to how
commands are registered), fired from the `Connected` event. Because that event runs on the MQTT event-loop task
and `publish()` enqueues onto that same task and waits, the callback **must not publish inline** — it only
pushes to the overwrite queue; the separate SYNC task does the publish.

### `BOOT`

- Queued once at boot (diagnostics + per-peripheral/function `error` feedback; **no configuration bodies**),
  delivered when MQTT connects. `NoRetain, QoS 1`.
- If the device reboots due to a faulty `requested` configuration **before** MQTT is established, that `BOOT`
  never reaches the server — accepted; the eventual `SYNC` after a successful boot (carrying the rejection
  code) is what informs the server.

### QoS 2 + clean session — an explicit assumption

`update` is subscribed and `sync` published at **QoS 2**. The MQTT driver forces a **clean session on every
reconnect**, so an `UPDATE` published while the device is offline is **not broker-queued** and is lost.
**This is by design and fine:** delivery of `UPDATE` is never relied upon. The device's `SYNC` on the next
successful connection re-advertises its `confirmed` fingerprints, and the server re-pushes whatever differs.
Reconciliation converges through SYNC-driven re-push, not through MQTT delivery guarantees.

### Rejection

Rejection is a single `google.rpc.Code` on `SYNC` (`INVALID_ARGUMENT`=3, `FAILED_PRECONDITION`=9,
`RESOURCE_EXHAUSTED`=8, `UNIMPLEMENTED`=12, `INTERNAL`=13; `OK`=0 is never sent — a matching fingerprint *is*
success). No message travels on the wire; human-readable detail goes to the `log` channel and an operator
correlates by device + time.

**Rejection is Phase 3.** It only becomes meaningful once we can apply-or-reject an `UPDATE` **atomically** (the
two-slot revert), and until then we **do not handle faulty configuration at all** — exactly as today. The code
subset and the "store rejection across the revert reboot, report once in the next `SYNC`, then clear" flow are
specified here so Phase 3 has a target, but nothing emits `rejected` before then.

## Migration

There is **no in-place NVS migration** and no reader for the old bare-body format. On the first boot of the new
firmware the slot layout is effectively empty, so the device reconciles from the server: an empty (or minimal)
`SYNC` prompts the server to re-push the full configuration set. Network configuration is untouched, so the
device still connects.

This means an upgraded device runs with **no device/function configuration** (defaults only) for the window
between first boot and the server's re-push + reboot. **We accept this as a cost of keeping the design simple:**
a firmware update already takes the device offline for a while (HTTP update requires connectivity and reboots),
so a short additional reconfiguration round-trip is not a meaningful regression. No one-time import of the old
`device-config`/`function-cfg` blobs is done.

### Upgrading into Phase 3 (no `config-state` yet)

The same "no migration, reconcile from empty" answer applies to the **Phase 1 → Phase 3** boundary, not just
Phase 0 → Phase 1. Phase 1 firmware has no `config-state` namespace and no `confirmed`/`requested` slot
pointer — it stores each configuration as a single envelope directly. A device last booted on Phase 1 firmware
therefore boots Phase 3 firmware with `config-state` entirely absent.

Two options were considered:

1. **Treat it as "no `confirmed` slot."** Boot as a no-functions device (defaults only), send an empty/minimal
   `SYNC`, and let the server re-push the full device + function configuration set, same as any other empty-slot
   boot.
2. **Migrate the existing Phase 1 envelopes into a synthesized `confirmed` slot.** This would require minting
   fingerprints for configuration the device itself already holds — but fingerprinting is exclusively the
   server's jurisdiction (see "Fingerprints are never computed on-device" above); the firmware has no legitimate
   way to produce one.

Option 2 is ruled out by that standing invariant, so **option 1 is what Phase 3 implements**: a missing
`config-state` (or a missing `confirmed` pointer within it) is handled identically to the empty-slot case above,
not as a distinct migration path. This is not a complete configuration-migration framework — just enough to
boot cleanly from old NVS and let the existing reconciliation loop (empty `SYNC` → server re-push) take it from
there, exactly as it already does for the Phase 0 → Phase 1 upgrade.

---

## Progress checklist

These phases are the firmware's own; they do **not** have to line up with the server spec's phase numbers.
Because there is no legacy dual stack, the firmware's new-protocol surface (`BOOT`/`SYNC`/`UPDATE`,
drop-retained, device+function reconciliation) lands as **one milestone** in Phase 1; atomicity + rejection +
device-configuration *authority transfer* land in Phase 3.

### Phase 0 — prerequisite

- [x] **`Configuration`/`Property` teardown** per [`configuration-as-schema.md`](configuration-as-schema.md):
      verbatim-JSON storage, persistence split from parsing, `store()` removed once `BOOT` stops echoing config
      bodies. **Do this first, on its own branch.** Landed in [#597](https://github.com/cornucopia-machines/ugly-duckling-firmware/pull/597).

### Phase 1 — the new protocol, happy path (single `confirmed` slot, no atomicity)

- [x] **Rename `settings` → device `configuration`.** `DeviceSettings` and the `device-config` load path,
      including type names and log strings. The `device` document becomes a reconciled configuration with a
      fingerprint like any other. `DeviceSettings` → `DeviceConfiguration` (type, file, and every derived local
      variable/parameter) across the boot path and per-device factories. The `init` message's wire-format
      `"settings"` JSON key is intentionally left alone here — it disappears entirely once the "Split `init`
      into `BOOT` + `SYNC`" item below removes configuration bodies from `init`.
- [x] **Per-configuration envelope + store.** Introduce the verbatim `{data, fingerprint, requestedAt}`
      envelope and a `StoredConfig` wrapper (read/write one envelope, expose `data`/`fingerprint`/`requestedAt`),
      separate from parsing. `requestedAt` is an ISO 8601 string (encoded like a schedule `start`).
      `ConfigEnvelope` (verbatim envelope + its `ArduinoJson::Converter`) and `StoredConfig` (NVS-backed wrapper,
      built on `NvsStore::get<ConfigEnvelope>`/`set<ConfigEnvelope>`) landed in
      `components/kernel/src/{ConfigEnvelope,StoredConfig}.hpp`. Not yet wired into `DeviceConfiguration`
      loading or function configuration at the time this item landed — function configuration got it via the
      `FunctionRegistry` item next, and `DeviceConfiguration` loading got it as part of the `…/update`
      subscription + handler item below (it needed a real device fingerprint to filter against).
- [x] **`FunctionRegistry`.** Evolve `FunctionManager` into the in-memory `name → {handle, fingerprint}` source
      of truth. Move the hot-reload logic out of the per-function `config` subscription into
      `reconfigure(name, envelope)`; record fingerprints on successful `configure(...)` at boot and on reload;
      expose `manifest()`.
      `FunctionManager` renamed to `FunctionRegistry` in `components/functions/src/functions/Function.hpp`;
      boot-time function config loading switched from bare-body `NvsConfiguration` to envelope-based
      `StoredConfig`, so the registry has a real fingerprint to record at creation. `reconfigure(name, envelope)`
      and `manifest()` land as designed, but aren't yet called by anything live — the `…/update` handler (next
      item) is what calls `reconfigure()` for real. The apply-and-track-fingerprint bookkeeping itself was
      split into `FunctionConfigTracker` (no NVS dependency, unit-tested with a fake `configureFn`), matching
      `ConfigEnvelope`/`StoredConfig`'s codec/IO split, since `FunctionRegistry` itself can't be built natively.
- [x] **`…/update` subscription + handler.** Subscribe the device root to `update` (`NoRetain, QoS 2`). Parse
      `configurations`; filter by held fingerprints (ignore the whole message if nothing differs). Persist
      changed envelopes to the (single) `confirmed` slot. **Device changed → reboot; functions-only changed →
      hot-reload via `FunctionRegistry.reconfigure`.** A config for a function not defined by the (post-update)
      device configuration is a faulty configuration (unhandled in Phase 1, exactly like a malformed body).
      The fingerprint-skip filter landed as a pure, NVS/MQTT-free function, `filterUpdate()` in
      `components/kernel/src/UpdateFilter.hpp`, unit-tested directly (native `unit-tests`) — mirroring the
      `FunctionConfigTracker` split. The handler itself lives in `registerUpdateHandler()` in
      `components/devices/src/Device.hpp`: it builds a `name → fingerprint` map from `FunctionRegistry.manifest()`
      plus the device configuration's own fingerprint, runs it through `filterUpdate()`, and branches on
      `deviceChanged`. Device configuration is now itself loaded at boot through a `StoredConfig` (the
      `"device-config"` key holds a verbatim envelope, not a bare body, closing the "not yet wired into
      `DeviceConfiguration` loading" gap noted in the `StoredConfig` item above) — this is what gives the
      handler a real confirmed device fingerprint to filter against, even though reporting it on `SYNC` is
      still Phase 3. On a device change, every changed entry (device *and* any bundled function envelopes) is
      persisted verbatim via `StoredConfig`/`FunctionRegistry.persist()` — deliberately skipping
      `FunctionRegistry.reconfigure()`'s live-apply step, since a device change reboots and boot re-derives
      everything from NVS — then `esp_restart()`. On a functions-only change, each entry goes through
      `FunctionRegistry.reconfigure()`; a throw (bad body, or a function name the current device configuration
      doesn't define) is caught and logged per-entry rather than crashing the MQTT dispatch task, matching how
      the old retained-topic subscription handled it before this rewrite.
- [ ] **Split `init` into `BOOT` + `SYNC`.** `boot` keeps all diagnostics + per-peripheral/function `error`
      feedback and **drops all configuration bodies** (`NoRetain, QoS 1`). `sync` is the fingerprint manifest
      from the `confirmed` slot / registry (`NoRetain, QoS 2`).
- [ ] **Connection-established hook + SYNC task.** Add a registered on-connect callback to `MqttDriver` (fires
      from the `Connected` event; must not publish inline). It pushes to a **single-element overwrite queue**; a
      dedicated SYNC task takes from it, **awaits `kernelReady`**, then publishes `SYNC`. Also publish `SYNC`
      immediately after a successful hot-reload `UPDATE` (via the same queue). No `SYNC` from `startDevice()`.
- [x] **Drop the retained `config` subscription.** Remove `functions/<name>/config` in `Function.hpp`; `UPDATE`
      is the only config-in path.
      Removed together with the `FunctionRegistry` item above, rather than as a separate commit, since keeping
      the old bare-body subscription alive even transiently would have meant two config-in paths writing two
      incompatible NVS formats to the same `function-cfg` keys. `FunctionInitParameters.functionRoot()` /
      `.mqttFunctionRoot` / `.mqttDeviceRoot` and `FunctionRegistry`'s own `mqttDeviceRoot` were dead code once
      the subscription was gone, so they were deleted too rather than left stubbed out.
- [ ] **No atomicity, no rejection.** A boot/apply failure is unrecoverable exactly as today; `rejected` is not
      emitted. (Deferred to Phase 3.)
- [ ] **Tests.** Split by tier — `NvsStore` talks to real `nvs.h` with no fake, so anything touching actual
      NVS or a live MQTT connection cannot run in `unit-tests` (native); anything touching a live MQTT
      connection cannot run in `embedded-tests` either (Wokwi without Mosquitto) — only `e2e-tests` has a
      broker (see `CLAUDE.md`: "MQTT/WiFi behavior goes in `test/e2e-tests/`").
    - **Native (`unit-tests`).** Requires `StoredConfig` to separate envelope JSON codec from NVS I/O (worth
      doing regardless of tests). Envelope round-trip (serialize/parse only, no NVS); fingerprint-skip
      filtering, including the whole-message no-op; `FunctionRegistry.manifest()`/`reconfigure()` against a
      fake function handle.
    - **Embedded (`embedded-tests`, Wokwi/IDF, no broker).** `StoredConfig` round-trip against real NVS;
      boot loading `confirmed` from real NVS across a real device reset. (Slot swap / requested-vs-confirmed
      boot selection for Phase 3 belongs here too — see below.)
    - **e2e (`e2e-tests`, Wokwi + Mosquitto).** Everything that goes over the wire: drive an `UPDATE`
      (function-only and device-change), assert reboot vs hot-reload, the `SYNC` manifest content, and
      SYNC-gated-on-boot-success; same-fingerprint `UPDATE` is a no-op (assert absence of
      reconfigure/reboot); the connection-established hook firing SYNC on connect/reconnect. **Blocked on
      [#596](https://github.com/cornucopia-machines/ugly-duckling-firmware/issues/596)** — the e2e pytest
      harness has no MQTT client today, only serial-output assertions, so none of this tier can be written
      until that fixture exists.

### Phase 3 — atomicity, rejection, and device-config authority

> The device-configuration **authority transfer** (server seeds device config at provisioning; retire
> `nvs/write` as the write mechanism) depends on the server side + `ble-provisioning.md`. The **atomicity +
> rejection** mechanism below is firmware-local and could land earlier if useful.

- [ ] **Two-slot `confirmed`/`requested` + `config-state` machine.** Implement the staging/commit/revert flow:
      stage changes into `requested`, mark `pending`→`attempted`, commit on success, revert to `confirmed` on
      failure across a reboot. `config-state` records `confirmed`/`requested`/`rejection`. A missing
      `config-state` namespace (device last booted on Phase 1 firmware) is handled as "no `confirmed` slot" —
      boot no-functions, empty `SYNC` — **not** as a migration of the old single-envelope layout (see
      *Migration* → "Upgrading into Phase 3").
- [ ] **Boot-time apply detection + revert.** Detect a `requested` set that fails to boot (including a crash
      that leaves it `attempted`) and revert to `confirmed`, recording the rejection.
- [ ] **Rejection reporting.** Persist the `google.rpc.Code` across the revert reboot; include it in the next
      `SYNC`, then clear it (report-once). Map parse/validation → `INVALID_ARGUMENT`, unknown function type →
      `UNIMPLEMENTED`, NVS-full → `RESOURCE_EXHAUSTED`, else `INTERNAL`.
- [ ] **`device` in the `SYNC` manifest + full `UPDATE` handling.** Report the `device` fingerprint; handle an
      `UPDATE` that bundles the `device` document plus every function it defines, persisted atomically into
      `requested` and applied across a reboot.
- [ ] **Retire the `nvs/write` + `restart` device-config path** once the server stops using it (keep raw
      `nvs/write` for debugging). Stop treating device-authored settings as ground truth.
- [ ] **Tests.** The `config-state` transitions (`confirmed`/`requested{pending,attempted,rejected}` →
      load/commit/revert) should be extracted as a pure function of state, not tested by physically crashing
      a Wokwi device mid-write. **Native (`unit-tests`):** table-driven tests over that function for every
      state combination, including "crashed while `attempted`". **Embedded (`embedded-tests`):** slot swap
      and revert-to-`confirmed` against real NVS across a real reboot, seeding envelopes directly into NVS
      rather than via `UPDATE` (no broker needed for this). **e2e (`e2e-tests`, blocked on
      [#596](https://github.com/cornucopia-machines/ugly-duckling-firmware/issues/596)):** rejection code
      reported on the `SYNC` following a revert, then cleared (report-once).

### Phase 4 — hardening (deferred)

- [ ] **Known-good slot + boot-loop protection** for a configuration that *kills the device* before it can
      report anything (a bad-payload boot loop — distinct from the serialization livelock the never-hash design
      already rules out). A device-side watchdog around the first successful apply after an `UPDATE`.
- [ ] **`requestedAt`/LWW** — the device already persists and echoes `requestedAt`; multi-writer
      last-write-wins rules (BLE-direct writes) are designed in `device-protocol-v2.md`, not here.
