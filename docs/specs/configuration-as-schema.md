# Configuration as schema — reduce `Configuration`/`Property` to parse-and-validate

> Status: **Not started.** This is a **prerequisite refactor** for
> [`config-reconciliation.md`](config-reconciliation.md): that plan assumes the model described here is
> already in place (configuration bodies stored/echoed as verbatim JSON, persistence separated from parsing).
> Land this first on its own branch, then return to the reconciliation work.
>
> One-line summary: **`Configuration` should declare a schema, supply defaults, and parse+validate a JSON
> object into an immutable typed value — nothing more. Persisting and printing configuration JSON moves out of
> `Configuration` and is handled verbatim.**

## Why

`Configuration`/`Property` ([`Configuration.hpp`](../../components/kernel/src/Configuration.hpp)) were designed
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
  - [`NvsConfiguration.hpp:44`](../../components/kernel/src/NvsConfiguration.hpp) — a thin `store()` pass-through.
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

Ordered for the branch. Most of the teardown lands independently; the **one** cross-spec dependency is the
final removal of `store()`, whose last callers are the `init`-message population sites — those disappear when
[`config-reconciliation.md`](config-reconciliation.md) Phase 1 splits `init` into `BOOT` (config bodies
dropped). Sequence this branch so `store()` deletion is the hand-off point to the reconciliation work.

- [ ] **Split parse from persist.** Separate "parse a `JsonObject` into a typed snapshot" (stays with
      `Configuration`) from "persist the raw body" (moves out). Reduce/retire `NvsConfiguration`'s
      parse-and-persist coupling; repoint the boot-time function/peripheral/device load paths so parsing takes a
      `JsonObject` and persistence is a caller concern.
- [ ] **Store the configuration body verbatim.** Persist and print the JSON exactly as received; stop
      re-serializing from a `Configuration`. This is the behavior the fingerprint contract depends on.
- [ ] **Snapshot semantics for `Property`/`ArrayProperty`/`NamedConfigurationEntry`.** Parsing yields a fresh
      immutable snapshot; a re-configure parses a new `Configuration` rather than mutating one. Drop the
      in-place-reload contract (`reset()`, mutate-on-`load`).
- [ ] **Trim the mutable/optional accessor surface.** Audit `getIfPresent()`/`getOrDefault()`/`hasValue()`
      callers; keep only what schema consumers genuinely need at parse time, drop the rest.
- [ ] **Verbatim JSON for diagnostics.** Anywhere configuration was printed via `store()`, print the stored raw
      JSON instead.
- [ ] **Remove `store()` from the `Configuration` hierarchy** (`ConfigurationSection`, `Property`,
      `ArrayProperty`, `NamedConfigurationEntry`). Gate on the `init`/`BOOT` split having dropped the last
      callers ([`Device.hpp:612`](../../components/devices/src/Device.hpp),
      [`Manager.hpp:208`](../../components/kernel/src/Manager.hpp),
      [`Function.hpp:83`](../../components/functions/src/functions/Function.hpp),
      [`NvsConfiguration.hpp:44`](../../components/kernel/src/NvsConfiguration.hpp)); this is the coordination
      point with the reconciliation branch.
- [ ] **Tests.** Keep/adjust `ConfigurationTest` for parse+defaults+validation; drop the
      round-trip-serialization assertions (`ConfigurationTest.cpp:29`).

## Non-goals

- **Deleting `Configuration`/`Property` outright.** That's the eventual direction, not this change. This spec
  only removes the mutable/serialization machinery so reconciliation can treat configurations as throw-away
  parsed snapshots over verbatim JSON.
- **Any wire/protocol change.** This is an internal representation refactor; it ships independently and is
  observable only as "the device no longer echoes config bodies via re-serialization."
