# Configuration as schema — reduce `Configuration`/`Property` to parse-and-validate

> Status: **Done.** Landed together with the `store()` removal (see note at the end of the work outline):
> `init`-message config echoing now uses verbatim JSON everywhere, so there was no reason to keep the
> serialization machinery around as dead code in the meantime. [`config-reconciliation.md`](config-reconciliation.md)
> can now build on this model (configuration bodies stored/echoed as verbatim JSON, persistence separated from
> parsing).
>
> One-line summary: **`Configuration` declares a schema, supplies defaults, and parses+validates a JSON object
> into an immutable typed value — nothing more. Persisting and printing configuration JSON happens verbatim,
> outside `Configuration`.**

## Why

`Configuration`/`Property` ([`Configuration.hpp`](../../components/kernel/src/config/Configuration.hpp)) were designed
in the early days as **long-lived, mutable** objects: a `Property` holds a current value you can read at any
time, `load()` mutates it in place, and the whole tree can serialize itself back to JSON via `store()`. That is
no longer how the firmware uses them.

Today we:

- **Read a configuration once**, at initialization and again inside `HasConfig::configure()`, and we do **not**
  retain references to `Property` objects afterwards. The "live, always-current value" semantics buy us nothing.
- Only call `store()` to **echo configuration back into the `init` message** — and
  [`config-reconciliation.md`](config-reconciliation.md) removes configuration bodies from that message
  entirely (the new `BOOT` message carries diagnostics only). After that split, `store()` has **no production
  callers left**.

So `Configuration` carries mutable state and serialization machinery we don't need, and the reconciliation work
wants the opposite contract: **store and print whatever JSON the server sent, verbatim**, and treat a parsed
`Configuration` as a throw-away typed view over one snapshot. `Configuration` and `Property` are, frankly,
overengineered remnants; the long-term intent is to remove them, but this refactor just dumbs them down enough
to unblock reconciliation.

## Where this bites today (grounding)

- **`store()` (serialization) has only init-message callers**, all of which
  [`config-reconciliation.md`](config-reconciliation.md) removes:
  - [`Device.hpp:612`](../../components/devices/src/Device.hpp) — `settings->store(device)` (device
    configuration into `init`).
  - [`Manager.hpp:208`](../../components/kernel/src/Manager.hpp) — `settings->params.store(initJson)`
    (peripheral/function params into `init`).
  - [`Function.hpp:83`](../../components/functions/src/functions/Function.hpp) —
    `config->store(initConfigJson)` (function config body into `init`).
  - [`NvsConfiguration.hpp:44`](../../components/kernel/src/config/NvsConfiguration.hpp) — a thin `store()` pass-through.
  - [`ConfigurationTest.cpp:29`](../../components/kernel/test/ConfigurationTest.cpp) — round-trip test.
- **`NvsConfiguration` couples parse and persist**: its constructor reads NVS → `config->load(...)`, and
  `update()` does `config->load(json)` **and** `nvs->setJson(key, json)` in one call. Reconciliation needs
  these two responsibilities split (parse a snapshot vs. persist the raw envelope).
- **Mutable `Property` state** (`value`/`configured`, `reset()`, `getIfPresent()`/`getOrDefault()`) exists to
  support in-place reload. With snapshot semantics, a fresh `Configuration` is parsed per apply and discarded;
  no in-place mutation is required.

## Target model

- **`Configuration` = schema + defaults + parse/validate → immutable typed value.**
  - Keep the declaration surface: `ConfigurationSection`, `Property<T>`, `ArrayProperty<T>`,
    `NamedConfigurationEntry<T>`, `HasConfig<TConfig>`, and the ArduinoJson `Converter`s (durations,
    `JsonAsString`, `system_clock::time_point`, …). Schemas already written across peripherals/functions must
    keep compiling with minimal churn.
  - Parsing a JSON object yields a **snapshot**: values + defaults resolved once, then treated as immutable. A
    re-configure parses a **new** `Configuration` rather than mutating an existing one.
  - **Drop the serialization path** (`store()` on every entry type) once its init-message callers are gone.
  - **Drop in-place mutation** semantics (`reset()`, the mutate-on-`load` contract). Whether `Property` keeps a
    private `value` internally is an implementation detail; the point is nothing outside relies on it changing
    after parse.
  - Keep validation: a body that fails to parse/validate throws `ConfigurationException` (unchanged), which the
    reconciliation apply path turns into a failure.
- **Raw JSON is the source of truth for storage and diagnostics.** The configuration body is stored and echoed
  **verbatim** (as the server sent it), never re-serialized from a `Configuration`. This is what makes the
  fingerprint contract safe end-to-end: the device round-trips bytes it does not reinterpret. Diagnostics that
  used to print `store()` output print the stored raw JSON instead.
- **Persistence separates from parsing.** `NvsConfiguration`'s dual role is split:
  - parsing/validation stays with `Configuration` (given a `JsonObject`, produce a typed snapshot);
  - persistence moves to the reconciliation layer's per-configuration store (the envelope wrapper described in
    [`config-reconciliation.md`](config-reconciliation.md)), which owns the raw `data` JSON plus its
    `fingerprint`/`requestedAt`.

## Work outline

- [x] **Split parse from persist.** `NvsConfiguration<TConfiguration>` and `loadConfigFromNvs` now construct a
      fresh typed snapshot on every load, and expose the raw `JsonDocument` they parsed from (`getRawJson()` /
      the out-param on `loadConfigFromNvs`) so callers can echo it verbatim instead of re-deriving it.
- [x] **Store the configuration body verbatim.** `Device.hpp`, `Manager.hpp`, and `Function.hpp` now embed the
      raw JSON retained from parsing directly into the `init` message instead of calling `store()`.
- [x] **Snapshot semantics for `Property`/`ArrayProperty`/`NamedConfigurationEntry`.** `load()` no longer relies
      on `reset()` — a fresh instance already starts in its default/unconfigured state, so absent JSON fields are
      simply left alone. `NvsConfiguration::update()` builds a new snapshot and swaps the `shared_ptr` rather than
      mutating the existing one in place.
- [x] **Trim the mutable/optional accessor surface.** Audited `getIfPresent()`/`getOrDefault()`/`hasValue()` —
      every one has a live caller (`ChickenDoor`/`PlotController` configure(), `UglyDucklingMk6Base`), so nothing
      to drop. The unused `secret` masking parameter on `Property` (never passed `true` anywhere) was dropped
      along with `store()`.
- [x] **Verbatim JSON for diagnostics.** Covered by the same call-site changes as "store verbatim" above — there
      were no other `store()`-based print sites.
- [x] **Remove `store()` from the `Configuration` hierarchy** (`ConfigurationSection`, `Property`,
      `ArrayProperty`, `NamedConfigurationEntry`, and the `NvsConfiguration` passthrough). This landed in the same
      branch as the verbatim-echo changes above rather than being deferred to the reconciliation branch: once
      `Device.hpp`/`Manager.hpp`/`Function.hpp` stopped calling it, `store()` had zero callers, and there was no
      reason to leave dead serialization code in the tree waiting for a coordinated deletion. The `init` message's
      wire shape (keys present, retention/QoS) is unchanged — only how the `settings`/`params`/`config` bodies are
      produced (verbatim JSON instead of re-serialized from a `Configuration`) is different, matching the
      non-goal below.
- [x] **Tests.** `ConfigurationTest` keeps parse+defaults+validation coverage; the round-trip-serialization
      assertions and the `toString()` helper were dropped.

## Non-goals

- **Deleting `Configuration`/`Property` outright.** That's the eventual direction, not this change. This spec
  only removes the mutable/serialization machinery so reconciliation can treat configurations as throw-away
  parsed snapshots over verbatim JSON.
- **Any wire/protocol change.** This is an internal representation refactor; it ships independently and is
  observable only as "the device no longer echoes config bodies via re-serialization."
