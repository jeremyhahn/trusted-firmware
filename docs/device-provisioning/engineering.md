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

A separate surface touches no element and opens no port — it bakes the
platform-reserved EEPROM block into a `.eep` image before the device is ever
flashed (`sdk/go/platform.go`, package `aggeneric`):

```go
func BuildPlatformBlock(serial [SerialLen]byte, pubkeyHash [PubkeyHashLen]byte,
	operatorPubkey [OperatorPubkeyLen]byte) [PlatformBlockLen]byte
```

`SerialLen` (16), `PubkeyHashLen` (32), `OperatorPubkeyLen` (64, issue #4) and
`PlatformBlockLen` (192, grew from 128 when `operatorPubkey` was added) mirror
`include/ag/boot.h`'s `AG_BOOT_SERIAL_LEN`/`AG_BOOT_MEASURE_LEN`/
`AG_BOOT_OPERATOR_PUBKEY_LEN`/`AG_BOOT_EE_LEN`. `operatorPubkey` is the
`AUTH_CHALLENGE`/`AUTH_RESPONSE` session's trust anchor — a P-256 point, X‖Y
— and is meaningful only on the element tier; a soft-tier or `CRYPTO=none`
image never reads it, so the zero value is correct there. CLI:
`sdk/go/cmd/agprov --bake-eeprom` — writes a partial Intel HEX file
(`sdk/go/cmd/agprov/eeprom.go`) covering only the platform block's 192
addresses, which is what makes it safe: `avrdude -U eeprom:w:file:i` writes
only the addresses a hex file names and leaves the rest of the part's EEPROM
untouched.

**The programmer matters, not just the file.** The `.eep` file this tool
writes has been verified correct offline (byte-for-byte, against
`ag_boot_platform_parse`'s own decode — see the test row below). On a real
Mega 2560 with its factory STK500v2 bootloader, `avrdude -c wiring -U
eeprom:w:platform.eep:i` was observed to corrupt the write in transit: bytes
`0x00`–`0x1b` landed, the rest of the record stream was dropped, and avrdude
reported a verification mismatch (chip-erase-based terminal-mode writes also
failed outright). The resulting on-device block fails its CRC, and
`ag_platform_id_init` correctly treats a bad-CRC block as unprovisioned
rather than as a forged identity — that is the firmware working as designed,
not evidence the bake was fine. It was not. Baking this image therefore
requires an ISP programmer. `ag-boot` exposes only PING, OTA and RESET
(`boot/boot_session.c`), and the application's `EEPROM_WRITE` refuses the
region unconditionally, so neither offers another route. The stock
serial bootloader's programmer type (`-c wiring`/`-c arduino`) is not a
supported way to write this block. See [`user.md`](user.md), step 1.

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

**Resolved** (issue #6): the device serial is baked into a `.eep` image
BEFORE the device is ever flashed, by `agprov --bake-eeprom`
(`sdk/go/cmd/agprov/eeprom.go`, `sdk/go/platform.go`) — never over the wire.
`sdk/go/commands.go`'s `EEPROMWrite` still refuses every address below
`EEPROMReservedEnd` (`0xC0`) unconditionally, and that refusal was kept
absolute rather than given a provisioning exemption: a narrower hole is
still a hole. `--bake-eeprom` opens no serial port and needs no
`AG_FEATURE_PROVISION` image; it is a pure file operation, run once, before
any of the fuse/flash/`agprov --commit` steps below. See
[`cli.md`](cli.md) and [`user.md`](user.md) for where it fits in the
sequence.

The same `--bake-eeprom` step now also bakes the `AUTH_CHALLENGE`/
`AUTH_RESPONSE` operator public key (issue #4, `--operator-pubkey`) into the
same block, for the same reason: the reserved-region write refusal is
absolute, so this is the only place either value can be set, and one bake
step doing both keeps `agprov`'s pre-flash sequence a single operation.

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
| Go unit | `sdk/go/platform_test.go` | `BuildPlatformBlock`'s layout, CRC coverage and tamper detection |
| Firmware unit (cross-language) | `test/unit/boot/test_boot_platform.c`'s `test_a_baked_eeprom_image_is_decoded_correctly` | a LITERAL byte-for-byte capture of what `agprov --bake-eeprom` actually wrote, decoded by `ag_boot_platform_parse` — the two implementations of one layout checked against each other's output, not just against themselves |
| Simulator / agsim | `sdk/go/agsim_test.go` | wire-level EEPROM reserved-region enforcement (`EEPROMWrite` at `0x000`, `0x07F`, `0x100`) exercised against the real firmware build, not a model |
| Integration (simavr) | `test/integration/simavr/boot_harness.c`'s `test_signed_image_boots` | a serial baked into the emulated EEPROM reaches `ATTEST`'s `DeviceID` over the real wire, through `ag_platform_id_init`, on the real compiled application — end to end, not mocked |

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
