# Device provisioning — engineering

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

See [`architecture.md`](architecture.md) for component boundaries and trust
model, [`cli.md`](cli.md) for `agprov` usage, [`api.md`](api.md) for the wire
commands.

## Exported surface

Firmware (`src/caps/provision.c`, `src/caps/provision.h`, compiled only when
`AG_FEATURE_PROVISION=1`):

```
ag_status_t ag_prov_cmd_info(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_prov_cmd_write_config(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_prov_cmd_lock(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_prov_cmd_genkey(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_prov_cmd_pubkey(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_prov_cmd_seed_uds(ag_reader_t *r, ag_writer_t *w);
```

Go SDK (`sdk/go/provision.go`, package `aggeneric`):

```go
func (d *Device) ProvInfo(ctx context.Context) (*ElementInfo, error)
func (d *Device) ProvWriteConfig(ctx context.Context, block uint8, data []byte) error
func (d *Device) ProvLock(ctx context.Context, zone uint8, summaryCRC uint16) error
func (d *Device) ProvGenKey(ctx context.Context, slot uint8) ([]byte, error)
func (d *Device) ProvPubKey(ctx context.Context, slot uint8) ([]byte, error)
func (d *Device) ProvSeedUDS(ctx context.Context, slot uint8) error

type ElementInfo struct {
	Revision     [4]byte
	Serial       [9]byte
	ConfigLocked bool
	DataLocked   bool
	ConfigBlock0 [32]byte
}

const (
	ZoneConfig uint8 = 0x00
	ZoneData   uint8 = 0x01
	SlotDeviceID uint8 = 0
	SlotUDS      uint8 = 1
)
```

CLI: `sdk/go/cmd/agprov` (binary `agprov`) — see [`cli.md`](cli.md).

## Invariants

- **`ProvLock` refuses a zero summary CRC.** The element's own no-CRC lock form
  locks without checking the zone contents; a zero CRC is refused in both
  `ag_prov_cmd_lock` (`AG_ERR_BAD_ARGC`) and `Device.ProvLock`
  (`ArgRangeError`), so the check cannot be bypassed from either side of the
  wire.
- **`ProvWriteConfig` never touches blocks 0 or 1.** They hold the serial
  number, revision and OTP mode; `ag_prov_cmd_write_config` returns
  `AG_ERR_PROTECTED_REGION` for `block < 2`, so a broken template fails at the
  firmware boundary rather than as an opaque element execution error.
- **`ProvGenKey` and `ProvPubKey` are separate commands, not modes of one.**
  `ProvGenKey` replaces the DeviceID; `ProvPubKey` only recomputes the public
  point from the key already in the slot. This split is what makes
  `agprov --verify` safe to run against an already-provisioned part — no
  verify path can regenerate the identity it is checking.
- **`ProvSeedUDS` takes a slot and no key material.** The UDS goes
  Random → TempKey → slot entirely inside the element (`ag_prov_cmd_seed_uds`);
  the host never sees it, and the SRAM copy is scrubbed with `memset` whether
  or not the write succeeded.
- **Both slot constants are fixed, not caller-supplied policy.** `ProvGenKey`
  and `ProvPubKey` reject any `slot != SlotDeviceID` (`AG_ERR_RANGE`);
  `ProvSeedUDS` rejects any `slot != SlotUDS`. A template cannot redirect the
  DeviceID key or the UDS into a slot the rest of the firmware never reads
  from.
- **Locking is irreversible and the SDK does not pretend otherwise.** There is
  no `ProvUnlock`. Once `ConfigLocked` or `DataLocked` is true on a part, no
  call in this surface can change it.

### The `--commit` sequence

`runCommit` in `sdk/go/cmd/agprov/steps.go` performs, in this order — the only
order the element accepts, since slot writes require a locked config zone and
the data zone cannot lock before the config zone does:

1. write each template config block (`ProvWriteConfig`)
2. lock the config zone (`ProvLock`, `ZoneConfig`) — **irreversible**
3. generate the DeviceID key in the element (`ProvGenKey`, `SlotDeviceID`)
4. seed the UDS from the element's TRNG (`ProvSeedUDS`, `SlotUDS`)
5. lock the data zone (`ProvLock`, `ZoneData`) — **irreversible**

If the config zone is already locked, steps 1–2 are skipped. If the data zone
is already locked, `runCommit` refuses outright: the part cannot be
reconfigured.

**Known gap, not a deliberate omission** (tracked as issue #6):
`include/ag/boot.h`'s platform-EEPROM layout comment describes `device_serial`
(offset 52) as "written by agprov, step 7." No such step exists in the code: `runCommit` has no EEPROM
step, and `sdk/go/commands.go`'s `EEPROMWrite` refuses every address below
`EEPROMReservedEnd` (`0x80`) unconditionally — not gated by
`AG_FEATURE_PROVISION` — so no wire command can write that region on any image.
The device serial must currently be baked into the EEPROM image before
flashing; treat any document or comment describing `agprov` as writing it as
out of date until a command exists.

## Error semantics

| Status | Meaning here | Raised by |
|---|---|---|
| `AG_ERR_NACK` (10) | the element did not answer the wake | every `ag_prov_cmd_*` on `ag_atecc_wake()` failure — not reported as "no element," since a miswired or overclocked bus is indistinguishable from an absent part |
| `AG_ERR_RANGE` (11) | a block index, zone id, or slot id outside the permitted set | `ag_prov_cmd_write_config` (block ≥ 4), `ag_prov_cmd_lock` (unknown zone), `ag_prov_cmd_genkey`/`_pubkey` (slot ≠ `SlotDeviceID`), `ag_prov_cmd_seed_uds` (slot ≠ `SlotUDS`) |
| `AG_ERR_BAD_ARGC` (5) | wrong payload length, or a zero summary CRC | `ag_prov_cmd_write_config` (no data), `ag_prov_cmd_lock` (CRC == 0) |
| `AG_ERR_PROTECTED_REGION` (16) | write targets config block 0 or 1 | `ag_prov_cmd_write_config` |
| `AG_ERR_UNKNOWN_CMD` (4) | production image — no provisioning handler compiled in | any `0x70`–`0x75` command against a device built with `AG_FEATURE_PROVISION=0` |
| `AG_ERR_NOT_PROVISIONED` (15) | attestation attempted on an element with no UDS | not raised by this package; raised by the attestation path when steps 4–5 of `--commit` never completed — see `docs/security/threat-model.md` |

On the Go side, a short or malformed reply is `*ShortReplyError` (e.g.
`ProvInfo` needs 38 bytes: 4 revision + 32 config block 0 + 2 lock bytes), and
an out-of-range caller argument is `*ArgRangeError` — both raised before any
frame is sent where the check does not require the device.

## Configuration

- **Build-time gate:** `AG_FEATURE_PROVISION` (`include/ag/config.h`), default
  `0`. Set to `1` via `mk/avr.mk`'s `PROV_DEFS` or `mk/host.mk`'s equivalent
  flag only for a provisioning-jig image; a fielded image must never carry it.
- **Template:** a `key = value` file (not YAML — see
  [`architecture.md`](architecture.md) for why), parsed by
  `sdk/go/cmd/agprov/template.go`. Required keys: `name`, `block<N>` (32 bytes
  hex, N ≥ 2), `config_lock_crc`, `data_lock_crc` (both non-zero). See
  [`api.md`](api.md) for the format and `sdk/go/templates/atecc608a-dice.conf`
  for the shipped example.

## Test strategy

| Tier | Location | Covers |
|---|---|---|
| Firmware unit | `test/unit/test_provision.c`, linked against `test/model/atecc608a.c` | every handler against a software model of the element — block refusal, CRC-required lock, slot range checks, out-of-order lock, seeding never exposing key material |
| Go unit | `sdk/go/provision_test.go` | `ProvInfo` payload decoding (both lock-byte polarities, truncated/missing-lock-byte replies, a production image's `ErrUnknownCommand`), `ProvLock`'s zero-CRC refusal, `ProvWriteConfig`'s size check |
| Simulator / agsim | `sdk/go/agsim_test.go` | wire-level EEPROM reserved-region enforcement (`EEPROMWrite` at `0x000`, `0x07F`, `0x100`) exercised against the real firmware build, not a model |

Run `test-provision`-equivalent unit targets per the top-level `Makefile`
conventions in this repo's `CLAUDE.md`; there is no dedicated
`integration-test-provision` target as of this writing — provisioning's
cross-implementation coverage runs through the simulator suite above.

## Observability

`agprov` is the only operator-facing signal: it prints the element's revision,
serial, and lock state (`report`) before any mode runs, and prints each
`--commit` step and the resulting DeviceID public key as it executes
(`runCommit`). There is no separate provisioning log; the operator is expected
to capture stdout as the provisioning record, per
[`architecture.md`](architecture.md)'s trust-boundary note that the record,
not the device, is the authority.
