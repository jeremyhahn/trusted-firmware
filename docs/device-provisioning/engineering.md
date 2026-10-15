# Device provisioning — engineering

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-10-15
> Conflicts with this document are defects. Fix them here first.

See [`architecture.md`](architecture.md) for component boundaries and trust
model, [`cli.md`](cli.md) for `tfwprov` usage, [`api.md`](api.md) for the wire
commands.

## Exported surface

Firmware (`src/caps/provision.c`, `src/caps/provision.h`, compiled only when
`TFW_FEATURE_PROVISION=1`):

```
tfw_status_t tfw_prov_cmd_info(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_write_config(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_lock(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_genkey(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_pubkey(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_seed_uds(tfw_reader_t *r, tfw_writer_t *w);
```

Go SDK (`sdk/go/provision.go`, package `tfw`):

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
flashed (`sdk/go/platform.go`, package `tfw`):

```go
func BuildPlatformBlock(serial [SerialLen]byte, pubkeyHash [PubkeyHashLen]byte,
	operatorPubkey [OperatorPubkeyLen]byte) [PlatformBlockLen]byte
```

`SerialLen` (16), `PubkeyHashLen` (32), `OperatorPubkeyLen` (64, issue #4) and
`PlatformBlockLen` (192, grew from 128 when `operatorPubkey` was added) mirror
`include/tfw/boot.h`'s `TFW_BOOT_SERIAL_LEN`/`TFW_BOOT_MEASURE_LEN`/
`TFW_BOOT_OPERATOR_PUBKEY_LEN`/`TFW_BOOT_EE_LEN`. `operatorPubkey` is the
`AUTH_CHALLENGE`/`AUTH_RESPONSE` session's trust anchor — a P-256 point, X‖Y
— and is meaningful only on the element tier; a soft-tier or `CRYPTO=none`
image never reads it, so the zero value is correct there. CLI:
`sdk/go/cmd/tfwprov --bake-eeprom` — writes a partial Intel HEX file
(`sdk/go/cmd/tfwprov/eeprom.go`) covering only the platform block's 192
addresses, which is what makes it safe: `avrdude -U eeprom:w:file:i` writes
only the addresses a hex file names and leaves the rest of the part's EEPROM
untouched.

**The programmer matters, not just the file.** The `.eep` file this tool
writes has been verified correct offline (byte-for-byte, against
`tfw_boot_platform_parse`'s own decode — see the test row below). On a real
Mega 2560 with its factory STK500v2 bootloader, `avrdude -c wiring -U
eeprom:w:platform.eep:i` was observed to corrupt the write in transit: bytes
`0x00`–`0x1b` landed, the rest of the record stream was dropped, and avrdude
reported a verification mismatch (chip-erase-based terminal-mode writes also
failed outright). The resulting on-device block fails its CRC, and
`tfw_platform_id_init` correctly treats a bad-CRC block as unprovisioned
rather than as a forged identity — that is the firmware working as designed,
not evidence the bake was fine. It was not. Baking this image therefore
requires an ISP programmer. `tfw-boot` exposes only PING, OTA and RESET
(`boot/boot_session.c`), and the application's `EEPROM_WRITE` refuses the
region unconditionally, so neither offers another route. The stock
serial bootloader's programmer type (`-c wiring`/`-c arduino`) is not a
supported way to write this block. See [`user.md`](user.md), step 1.

CLI: `sdk/go/cmd/tfwprov` (binary `tfwprov`) — see [`cli.md`](cli.md).

## Invariants

- **`ProvLock` refuses a zero summary CRC.** The element's own no-CRC lock form
  locks without checking the zone contents; a zero CRC is refused in both
  `tfw_prov_cmd_lock` (`TFW_ERR_BAD_ARGC`) and `Device.ProvLock`
  (`ArgRangeError`), so the check cannot be bypassed from either side of the
  wire.
- **`ProvWriteConfig` never touches blocks 0 or 1.** They hold the serial
  number, revision and OTP mode; `tfw_prov_cmd_write_config` returns
  `TFW_ERR_PROTECTED_REGION` for `block < 2`, so a broken template fails at the
  firmware boundary rather than as an opaque element execution error.
- **`ProvGenKey` and `ProvPubKey` are separate commands, not modes of one.**
  `ProvGenKey` replaces the DeviceID; `ProvPubKey` only recomputes the public
  point from the key already in the slot. This split is what makes
  `tfwprov --verify` safe to run against an already-provisioned part — no
  verify path can regenerate the identity it is checking.
- **`ProvSeedUDS` takes a slot and no key material.** The UDS goes
  Random → TempKey → slot entirely inside the element (`tfw_prov_cmd_seed_uds`);
  the host never sees it, and the SRAM copy is scrubbed with `memset` whether
  or not the write succeeded.
- **Both slot constants are fixed, not caller-supplied policy.** `ProvGenKey`
  and `ProvPubKey` reject any `slot != SlotDeviceID` (`TFW_ERR_RANGE`);
  `ProvSeedUDS` rejects any `slot != SlotUDS`. A template cannot redirect the
  DeviceID key or the UDS into a slot the rest of the firmware never reads
  from.
- **Locking is irreversible and the SDK does not pretend otherwise.** There is
  no `ProvUnlock`. Once `ConfigLocked` or `DataLocked` is true on a part, no
  call in this surface can change it.

### The `--commit` sequence

`runCommit` in `sdk/go/cmd/tfwprov/steps.go` performs, in this order — the only
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
BEFORE the device is ever flashed, by `tfwprov --bake-eeprom`
(`sdk/go/cmd/tfwprov/eeprom.go`, `sdk/go/platform.go`) — never over the wire.
`sdk/go/commands.go`'s `EEPROMWrite` still refuses every address below
`EEPROMReservedEnd` (`0xC0`) unconditionally, and that refusal was kept
absolute rather than given a provisioning exemption: a narrower hole is
still a hole. `--bake-eeprom` opens no serial port and needs no
`TFW_FEATURE_PROVISION` image; it is a pure file operation, run once, before
any of the fuse/flash/`tfwprov --commit` steps below. See
[`cli.md`](cli.md) and [`user.md`](user.md) for where it fits in the
sequence.

The same `--bake-eeprom` step now also bakes the `AUTH_CHALLENGE`/
`AUTH_RESPONSE` operator public key (issue #4, `--operator-pubkey`) into the
same block, for the same reason: the reserved-region write refusal is
absolute, so this is the only place either value can be set, and one bake
step doing both keeps `tfwprov`'s pre-flash sequence a single operation.

## Error semantics

| Status | Meaning here | Raised by |
|---|---|---|
| `TFW_ERR_NACK` (10) | the element did not answer the wake | every `tfw_prov_cmd_*` on `tfw_atecc_wake()` failure — not reported as "no element," since a miswired or overclocked bus is indistinguishable from an absent part |
| `TFW_ERR_RANGE` (11) | a block index, zone id, or slot id outside the permitted set | `tfw_prov_cmd_write_config` (block ≥ 4), `tfw_prov_cmd_lock` (unknown zone), `tfw_prov_cmd_genkey`/`_pubkey` (slot ≠ `SlotDeviceID`), `tfw_prov_cmd_seed_uds` (slot ≠ `SlotUDS`) |
| `TFW_ERR_BAD_ARGC` (5) | wrong payload length, or a zero summary CRC | `tfw_prov_cmd_write_config` (no data), `tfw_prov_cmd_lock` (CRC == 0) |
| `TFW_ERR_PROTECTED_REGION` (16) | write targets config block 0 or 1 | `tfw_prov_cmd_write_config` |
| `TFW_ERR_UNKNOWN_CMD` (4) | production image — no provisioning handler compiled in | any `0x70`–`0x75` command against a device built with `TFW_FEATURE_PROVISION=0` |
| `TFW_ERR_NOT_PROVISIONED` (15) | attestation attempted on an element with no UDS | not raised by this package; raised by the attestation path when steps 4–5 of `--commit` never completed — see `docs/security/threat-model.md` |

On the Go side, a short or malformed reply is `*ShortReplyError` (e.g.
`ProvInfo` needs 38 bytes: 4 revision + 32 config block 0 + 2 lock bytes), and
an out-of-range caller argument is `*ArgRangeError` — both raised before any
frame is sent where the check does not require the device.

## Configuration

- **Build-time gate:** `TFW_FEATURE_PROVISION` (`include/tfw/config.h`), default
  `0`. Set to `1` via `mk/firmware.mk`'s `PROV_DEFS` or `mk/host.mk`'s equivalent
  flag only for a provisioning-jig image; a fielded image must never carry it.
- **Template:** a `key = value` file (not YAML — see
  [`architecture.md`](architecture.md) for why), parsed by
  `sdk/go/cmd/tfwprov/template.go`. Required keys: `name`, `block<N>` (32 bytes
  hex, N ≥ 2), `config_lock_crc`, `data_lock_crc` (both non-zero). See
  [`api.md`](api.md) for the format and `sdk/go/templates/atecc608a-dice.conf`
  for the shipped example.

## The ATECC608C (MIKROE-6774 Click board)

The element the boards take is the ATECC608C-TFLXTLS on MikroE's ECC608 Trust
Click (MIKROE-6774). The build identifier stays `atecc608a`: the 608C runs the
same command set, opcodes, packet format, counters and wake timing as the 608A
(AN5078, DS00005078; the 608A-to-608B guide, DS40002237A). The driver changes
for the 608C are the I2C address, which is now board-profile data, and the
execution times, now one table in `src/crypto/atecc608a/atecc.h`.

| Fact | Value | Source |
|---|---|---|
| Part at the element address | ATECC608C-TFLXTLS, revision `00 00 60 05` | schematic v100 (U3); AN5078 |
| I2C address as shipped | `0x36`, changeable once by `UpdateExtra` | DS40002515B §3.1 |
| Other parts on the Click | ATECC608C-TNGTLS at `0x35`, ECC608-TMNGTLS at `0x38`, same bus | schematic v100 (U2, U4) |
| State as shipped | pre-provisioned (TrustFLEX): config and data zones locked, slot policies set by Microchip | DS40002515B page 1 and §3 |
| Supply / I/O | 2.0–5.5 V supply, 1.8–5.5 V I/O | DS40002515B page 1 |
| VCC select | JP1 "VCC SEL", 3V3 or 5V; set to 5V for the AVRs. The schematic does not show the shipped position | schematic v100 |
| Pull-ups | R5, R6, 4.7 kΩ to the Click's VCC | schematic v100 |
| Pins | mikroBUS SDA and SCL; Nano A4/A5, Mega D20/D21 by flying lead | schematic v100; `boards/*/board_profile.h` |
| Bus speed | 100 kHz in the profiles; the part is rated to 1 MHz, but the wake pulse needs ≤ 100 kHz | DS40002515B table 10-3 |
| KDF details, clock divider | under NDA; not used and not guessed. Execution times assume the 608A's default divider | — |

Sources:
[DS40002515B](https://ww1.microchip.com/downloads/aemDocuments/documents/SCBU/ProductDocuments/DataSheets/ATECC608C-TFLXTLS-CryptoAuthentication-Data-Sheet-DS40002515.pdf),
[DS40002513](https://ww1.microchip.com/downloads/aemDocuments/documents/SCBU/ProductDocuments/DataSheets/ATECC608C-CryptoAuthentication-Summary-Data-Sheet-DS40002513.pdf),
[AN5078, DS00005078](https://ww1.microchip.com/downloads/aemDocuments/documents/SCBU/ApplicationNotes/ApplicationNotes/AN5078-ATECC608B-to-ATECC608C-Migration-Guide-DS00005078.pdf),
[DS40002237A](https://ww1.microchip.com/downloads/en/Appnotes/Migrating-from-the-ATECC608A-to-the-ATECC608B-DS40002237A.pdf),
[ECC608 Trust Click schematic v100](https://download.mikroe.com/documents/add-on-boards/click/ecc608-trust-click/ecc608-trust-click-schematic.pdf).

**The slot map is an open owner decision.** The `--commit` sequence above
assumes a blank part. The TFLXTLS is not blank: its zones are locked, so
`tfwprov --commit` refuses it. Its slot policies (DS40002515B §3.2.3) do not
match ours:

- Slot 0 holds a permanent primary key that Microchip generated. It signs
  external messages, so it can serve as the DeviceID, but `ProvGenKey` cannot
  replace it.
- Slot 1, where `SlotUDS` points, is an internal-sign private key, not a secret,
  so the CDI HMAC fails. The element tier therefore answers `NOT_PROVISIONED`.
- A 32-byte secret fits only slot 5. That slot takes an encrypted write under
  the slot-6 I/O protection key, which needs GenDig, and the driver does not
  implement it.
- The secure-boot public key belongs in slot 15, not slot 2.

`SlotUDS` is pinned in `sdk/go/provision.go`, so moving it is a wire-visible
choice for the owner, not a driver fix. The shipped layout, the options and a
proposed answer are in [the slot-map ADR](adr-trustflex-608c-slot-map.md).

## Test strategy

| Tier | Location | Covers |
|---|---|---|
| Firmware unit | `test/unit/test_provision.c`, linked against `test/model/atecc608a.c` | every handler against a software model of the element — block refusal, CRC-required lock, slot range checks, out-of-order lock, seeding never exposing key material |
| Go unit | `sdk/go/provision_test.go` | `ProvInfo` payload decoding (both lock-byte polarities, truncated/missing-lock-byte replies, a production image's `ErrUnknownCommand`), `ProvLock`'s zero-CRC refusal, `ProvWriteConfig`'s size check |
| Go unit | `sdk/go/platform_test.go` | `BuildPlatformBlock`'s layout, CRC coverage and tamper detection |
| Firmware unit (cross-language) | `test/unit/boot/test_boot_platform.c`'s `test_a_baked_eeprom_image_is_decoded_correctly` | a LITERAL byte-for-byte capture of what `tfwprov --bake-eeprom` actually wrote, decoded by `tfw_boot_platform_parse` — the two implementations of one layout checked against each other's output, not just against themselves |
| Simulator / tfwsim | `sdk/go/tfwsim_test.go` | wire-level EEPROM reserved-region enforcement (`EEPROMWrite` at `0x000`, `0x07F`, `0x100`) exercised against the real firmware build, not a model |
| Integration (simavr) | `test/integration/simavr/boot_harness.c`'s `test_signed_image_boots` | a serial baked into the emulated EEPROM reaches `ATTEST`'s `DeviceID` over the real wire, through `tfw_platform_id_init`, on the real compiled application — end to end, not mocked |

Run `test-provision`-equivalent unit targets per the top-level `Makefile`
conventions in this repo's `CLAUDE.md`; there is no dedicated
`integration-test-provision` target as of this writing — provisioning's
cross-implementation coverage runs through the simulator suite above.

## Observability

`tfwprov` is the only operator-facing signal: it prints the element's revision,
serial, and lock state (`report`) before any mode runs, and prints each
`--commit` step and the resulting DeviceID public key as it executes
(`runCommit`). There is no separate provisioning log; the operator is expected
to capture stdout as the provisioning record, per
[`architecture.md`](architecture.md)'s trust-boundary note that the record,
not the device, is the authority.
