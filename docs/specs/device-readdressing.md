# Device Re-addressing — Firmware Side

> Status: **Plan.** The firmware half of the device re-addressing migration. The full design —
> unique ID format, network-config in the two-slot mechanism, SYNC/UPDATE delivery, dual-stack
> server, migration flow — is in the app repo:
> [`cornucopia-app/docs/specs/device-readdressing.md`](https://github.com/cornucopia-machines/cornucopia-app/blob/main/docs/specs/device-readdressing.md).
> This spec covers only the firmware changes.

## Context

The MQTT topic root is changing from `{location}/devices/ugly-duckling/{instance}/...` to
`d/{uniqueId}/...`, where `uniqueId` is a permanent GrowMachine identifier — 11 characters of
Base-58 encoding 64 random bits (e.g. `2N4GcBkr7ER`). The migration happens per-device: the server
pushes a new network-config (with the `id` field and fresh certs) via the existing SYNC/UPDATE
protocol, and the firmware applies it through the two-slot config mechanism.

The firmware needs to:

1. Migrate old network-config from the NVS `config` namespace into the config slot system
2. Use the `id` field from network-config to choose the topic root
3. Use the `id` field for the MQTT client ID
4. Include network-config in the SYNC manifest and handle it in UPDATE

## Precondition: clean config state before firmware upgrade

A firmware upgrade may only be applied when the config state is fully confirmed — i.e.
`configState.requested` is absent. This guarantees that on first boot with the new firmware, all
pre-existing configuration lives in the confirmed slot and there is no pending or attempted
request.

- [x] In the UPDATE handler, skip the firmware entry if `configState.requested` is present.
      Report the skipped firmware update as a `FailedPrecondition` (code 9) rejection via the
      existing `firmware.rejection` field in the next SYNC message. The config changes in the same
      UPDATE message (if any) are still processed normally.

      The server sees the `FailedPrecondition` rejection and retries the firmware update on a
      subsequent SYNC once the config settles.

      This is defense-in-depth — the server should also avoid pushing firmware to devices with
      pending config, but the firmware enforces the invariant independently.

## Changes

### 1. Bootstrap migration: NVS `config` namespace → confirmed config slot

On first boot after the firmware upgrade, the old network-config must move into the two-slot
config mechanism so SYNC/UPDATE can manage it. The precondition above guarantees that
`configState.confirmed` points to a valid slot with no pending request.

- [x] On boot, check if the confirmed config slot already has a `network` entry.
- [x] If not, read network-config from the old NVS `config` namespace (`network-config` key).
- [x] Create a config envelope with the old config data and the **sentinel fingerprint
      `"unsynced"`**. The sentinel is intentional: the config slot system echoes server-assigned
      fingerprints — the old network-config was never part of this system and has no real
      fingerprint. When the device SYNCs with `"unsynced"`, the server sees an unrecognized
      fingerprint with no pending `requested` and generates a new network-config on demand.
- [x] Write the envelope into the **confirmed** config slot as the `network` entry. This is a
      direct write into the confirmed slot's NVS namespace (adding a `"network"` blob alongside
      the existing `"device"` blob), not a staged update — the old config already worked, so
      confirming it is safe.
- [x] This migration must complete **before** the firmware reads network-config to establish the
      MQTT connection. Boot sequence: migrate old NVS → read confirmed slot → extract
      network-config → connect to MQTT → SYNC.

#### NVS cleanup

- [x] On every boot, if the confirmed config slot has a `network` entry, delete any lingering
      `network-config` key from the old NVS `config` namespace (best-effort). This ensures
      orphaned keys from interrupted migrations eventually get cleaned up.

### 2. Topic root selection

- [x] Read network-config from the `network` entry in the confirmed config slot.
- [x] If the config has an `id` field (non-empty string): use `d/{id}/...` as the MQTT topic root
      (e.g. `d/2N4GcBkr7ER/boot`, `d/2N4GcBkr7ER/sync`, etc.).
- [x] If `id` is absent: use the legacy topic root
      `{location}/devices/ugly-duckling/{instance}/...` (backward compatible — the device hasn't
      been migrated yet).

The topic root selection happens once at boot (or reconnect). The `id` is read from the same
config slot data used for host/port/certs.

### 3. MQTT client ID

- [x] If the config has an `id` field (non-empty string): use the `id` as the MQTT client ID
      (e.g. `2N4GcBkr7ER`).
- [x] If `id` is absent: use the legacy client ID format (`ugly-duckling-{macAddress}`).

The client ID is determined at the same time as the topic root, from the same config.

### 4. Network-config in SYNC manifest

- [x] Include a `network` entry in the SYNC manifest, alongside `device` and function entries.
      The value is the fingerprint from the `network` envelope in the config slot.
- [x] `filterUpdate()` must include the network fingerprint in `heldFingerprints` so that
      no-op network updates are filtered out.

This is reported on every SYNC (boot, reconnect, post-UPDATE), same as other config entries.

### 5. Network-config in UPDATE

- [x] When the UPDATE message includes a `network` entry in its `configurations` map, handle it
      alongside device-config and function-config entries: write it to the requested config slot.
- [x] Add a `ConfigUpdateResult::NetworkChanged` variant. A network-config change triggers a
      reboot (same as `DeviceChanged`), since the MQTT connection parameters change.

A network-config change can share a requested slot with device-config and/or function-config
changes. The all-or-nothing slot rollback is safe regardless of the combination: the confirmed
slot always holds a consistent, known-good state. If the slot fails, the server re-delivers each
config type through its own mechanism. After reboot, the two-slot mechanism's strict apply runs:
if the new config slot applies successfully → commit as confirmed; if it fails → revert to the
old confirmed slot and reboot.

> **Known limitation — network rollback gap:** The current strict-boot mechanism validates
> device/function initialization, not MQTT connectivity. A bad network-config (wrong host, expired
> cert) would pass strict apply and be committed even though the device can't reach the server.
> This is an accepted risk — the server generates the certs and host, so invalid configs are
> unlikely. A future "delayed-confirm" mechanism (confirming config only after a successful SYNC
> round-trip) will close this gap for all config types, not just network.

### 6. New network-config shape

The new network-config delivered via UPDATE has a different shape than the old one:

**Old (in NVS `config` namespace):**

```jsonc
{
  "host": "mqtt.cornucopia-machines.eu",
  "port": 8883,
  "instance": "soil-probe-north",
  "location": "garden-3",
  "ntp": { "host": "pool.ntp.org" },
  "serverCert": ["..."],
  "clientCert": ["..."],
  "clientKey": ["..."],
}
```

**New (in config slot, via UPDATE):**

```jsonc
{
  "id": "2N4GcBkr7ER",
  "host": "mqtt.cornucopia-machines.eu",
  "port": 8883,
  "ntp": { "host": "pool.ntp.org" },
  "serverCert": ["..."],
  "clientCert": ["..."],
  "clientKey": ["..."],
}
```

- [x] `id` is a new field — used for topic root selection and MQTT client ID.
- [x] `instance` and `location` are **removed** — they were only needed for the old topic root.
      The firmware must handle their absence gracefully (they won't be in the new config).

The old network-config (migrated to the confirmed slot) still has `instance`/`location`; the
firmware uses them for the old topic root until the UPDATE replaces the config. `host`, `port`,
`ntp`, `serverCert`, `clientCert`, `clientKey` are unchanged in semantics.

### 7. Documentation

- [ ] Update [`docs/Configuration.md`](../Configuration.md) — document that network-config is now
      part of the two-slot config system, the `network` manifest entry, and the bootstrap migration
- [ ] Add the new topic root format (`d/{id}/...`) to any docs that reference the MQTT topic
      structure

### 8. Follow-up: remove legacy support

- [ ] Open an issue to remove pre-reconciled network-config support and the old MQTT topic once all
      devices are migrated (Phase 2 cleanup in the app-side spec). Covers: bootstrap migration code,
      NVS `config` namespace cleanup, legacy topic root fallback, `id`-absent branches in
      `selectTopicRoot()` / `selectClientId()`, and `instance`/`location` in `NetworkConfig`.

## Testing

### Unit tests (native Catch2, `test/unit-tests/`)

- [x] **Firmware decision — precondition** (`FirmwareUpdateDecisionTest`): `decideFirmwareUpdate`
      returns URL when no pending config request; skips (with `skippedDueToPendingConfig`) when
      config request is in flight; returns no-op when firmware entry is absent or version matches,
      regardless of config state
- [x] **Topic root selection**: `id` present → `d/{id}`, absent → legacy
      `{location}/devices/ugly-duckling/{instance}/…` (inlined in `NetworkConfig::getTopicRoot()`)
- [x] **Client ID selection**: `id` present → `ugly-duckling-{id}`, absent →
      `ugly-duckling-{macAddress}` (inlined in `startDevice()`)
- [ ] **Network config parsing**: both old shape (with `instance`/`location`, no `id`) and new
      shape (with `id`, no `instance`/`location`) parse correctly; missing fields handled
      gracefully
- [x] **Network fingerprint in `filterUpdate`** (`UpdateFilterTest`): `network` in
      `heldFingerprints` skips matching fingerprints and keeps mismatches
- [x] **Network entry triggers reboot** (`UpdateFilterTest`): `filterUpdate` treats a changed
      `network` entry like `device` — sets the flag that causes the handler to reboot

### Integration tests (Wokwi embedded / e2e)

- [x] **Bootstrap migration** (`BootstrapMigrationTest`, embedded NVS): old network-config in
      legacy NVS migrates to the confirmed config slot with sentinel fingerprint `"unsynced"`
- [x] **Already-migrated device** (`BootstrapMigrationTest`, embedded NVS): device that already
      has `network` in the confirmed config slot skips the migration; existing entry preserved
- [x] **NVS cleanup** (`BootstrapMigrationTest`, embedded NVS): orphaned `network-config` in
      old NVS is removed even when the confirmed slot already has a `network` entry
- [x] **No config anywhere** (`BootstrapMigrationTest`, embedded NVS): fresh device with neither
      legacy NVS nor slot entry → empty manifest, device uses default config
- [ ] **UPDATE delivery:** server sends UPDATE with `network` entry → device writes to requested
      slot → reboots → applies → connects on new topic → SYNCs with new fingerprint
- [ ] **Rollback:** UPDATE with bad network-config → apply fails → device reverts to confirmed
      slot → connects on old topic → SYNCs with rejection
- [ ] **Firmware update deferred on pending config:** UPDATE with firmware + config changes →
      config staged, firmware skipped → after config settles, firmware delivered on next UPDATE
- [ ] **Missing instance/location:** new network-config without `instance`/`location` fields →
      firmware handles gracefully (uses `id` for topic, doesn't try to read missing fields)

## Migration sequence (how this interacts with the server)

The full migration sequence is documented in the app-side spec. From the firmware's perspective:

1. **Server deploys Phase 0** — generates unique IDs and requested network-configs for all devices.
   No firmware changes yet; devices are unaffected.
2. **Firmware is upgraded per device** (this spec's work). On first boot:
   - Bootstrap migration runs (NVS → confirmed config slot with sentinel fingerprint `"unsynced"`).
   - Device connects on old topic, SYNCs with `network: "unsynced"`.
   - Server sees unrecognized fingerprint → generates and sends UPDATE with new network-config.
   - Device writes to requested slot, reboots.
   - Device applies new config, connects on `d/{id}/...`, SYNCs to confirm.
3. **Server confirms** the network-config. Device is now on the new topic permanently.
4. **Once all devices are migrated**, Phase 2 cleanup removes from firmware:
   - Bootstrap migration code (NVS → config slot)
   - NVS `config` namespace cleanup logic
   - Legacy topic root fallback (`{location}/devices/ugly-duckling/{instance}/...`)
   - `id`-absent branch in topic root and client ID selection
   - `instance` and `location` property handling in `NetworkConfig`
