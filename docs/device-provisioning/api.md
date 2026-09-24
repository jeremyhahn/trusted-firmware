# Device provisioning — API

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

Six command ids, `0x70`–`0x75`, reserved permanently on the wire (see
`docs/wire-protocol/architecture.md` for the frame format). Every one of them is
answered `AG_ERR_UNKNOWN_CMD` by a production image — see
[`architecture.md`](architecture.md) for why the gate is file selection, not a
runtime check.

## Commands

| Id | Name | Request payload | Reply payload |
|---|---|---|---|
| `0x70` | `PROV_INFO` | none | 38 bytes: `revision[4]` + `config_block_0[32]` + `lock_data[1]` + `lock_config[1]` |
| `0x71` | `PROV_WRITE_CONFIG` | 33 bytes: `block[1]` + `data[32]` | none |
| `0x72` | `PROV_LOCK` | 3 bytes: `zone[1]` + `summary_crc[2]` (little-endian) | none |
| `0x73` | `PROV_GENKEY` | 1 byte: `slot[1]` | 64 bytes: `public_point[64]` (X‖Y) |
| `0x74` | `PROV_PUBKEY` | 1 byte: `slot[1]` | 64 bytes: `public_point[64]` (X‖Y) |
| `0x75` | `PROV_SEED_UDS` | 1 byte: `slot[1]` | none |

`AG_CMD_ENTRY` in `src/core/commands.def` fixes both the min and max payload
size per command (e.g. `PROV_WRITE_CONFIG` is exactly 33 bytes both ways); a
request of the wrong length is rejected as `AG_ERR_BAD_ARGC` before the handler
runs.

### `PROV_INFO`

Reads the element's identity and lock state. Writes nothing — the only
provisioning command safe to send to a part you care about.

- `revision[4]`: die revision; byte 2 is `0x60` for the 608 family.
- `config_block_0[32]`: raw config zone block 0. The element's serial number is
  split across bytes `0..3` and `8..12` of this block (the element's own
  layout, not this firmware's).
- `lock_data[1]`, `lock_config[1]`: `0x55` means **unlocked**; any other value
  means locked. The polarity is the element's — the byte that looks like a
  flag being set is the zone being open.

### `PROV_WRITE_CONFIG`

Writes one 32-byte block of the config zone. `block` is a block index (4-byte
words × 8 = 32 bytes per block); only blocks `2` and `3` may be written —
blocks `0` and `1` hold the serial, revision, and OTP mode and are refused with
`AG_ERR_PROTECTED_REGION`. Refused once the config zone is locked.

### `PROV_LOCK`

Locks `zone` (`0x00` = config, `0x01` = data) after checking `summary_crc`
against the element's own summary of the zone's contents. **Irreversible.**
`summary_crc == 0` is refused (`AG_ERR_BAD_ARGC`) rather than treated as "lock
without checking" — the element supports that form, and this firmware never
sends it.

### `PROV_GENKEY`

Generates a new P-256 keypair inside `slot` (must be `0`, the DeviceID slot)
and returns the public point. The private half never leaves the element.
Requires the config zone to already be locked. **Replaces the device's
identity** — this is the command `--commit` calls once, never `--verify`.

### `PROV_PUBKEY`

Recomputes the public point from the key already in `slot` (must be `0`).
Generates nothing. This is what `--verify` calls, and the reason it is safe to
run against an already-provisioned part.

### `PROV_SEED_UDS`

Fills `slot` (must be `1`, the UDS slot) from the element's TRNG. Takes no key
material — the request payload is the slot id only. Internally: `Random` into
`TempKey`, then `Write` from `TempKey` into the slot, entirely inside the
element; the host never observes the value on the wire.

## Status codes specific to this surface

See [`engineering.md`](engineering.md) for the full table mapping each status
to the command(s) that raise it. In summary: `AG_ERR_NACK` (element did not
wake), `AG_ERR_RANGE` (bad block/zone/slot), `AG_ERR_BAD_ARGC` (wrong length or
zero CRC), `AG_ERR_PROTECTED_REGION` (write to block 0/1), `AG_ERR_UNKNOWN_CMD`
(production image).

## The provisioning template

`agprov --config` takes a `key = value` file, not YAML (see
[`architecture.md`](architecture.md) for why). Grammar, from
`sdk/go/cmd/agprov/template.go`:

```
name            = <string>                     # free-form, printed in --dry-run
block<N>        = <64 hex chars>                # N >= 2; exactly 32 bytes, written via PROV_WRITE_CONFIG
config_lock_crc = 0x<1-4 hex chars>             # non-zero; sent as PROV_LOCK's summary_crc for zone 0x00
data_lock_crc   = 0x<1-4 hex chars>             # non-zero; sent as PROV_LOCK's summary_crc for zone 0x01
```

Blank lines and lines starting with `#` are ignored. An unknown key, a
malformed block index, a block of the wrong length, or either CRC left at zero
is a load error — refused before any command reaches the device, since a typo
that silently did nothing would be a template that provisions a part
differently than it reads.

Example (`sdk/go/templates/atecc608a-dice.conf`, values are placeholders — see
that file's header before using it):

```
name = atecc608a-dice-hw-v1

block2 = 00000000000000000000000000000000000000000000000000000000000000ff
block3 = 00000000000000000000000000000000000000000000000000000000000000ff

config_lock_crc = 0x0001
data_lock_crc   = 0x0001
```

`block2` and `block3` encode the two slot configurations: slot 0 as a
non-exportable P-256 private key (the DeviceID), slot 1 as a never-readable key
usable only by the SHA/HMAC command (the UDS).

## What is out of scope for this surface

The device serial and the rest of the platform-reserved EEPROM region
(`include/ag/boot.h`, offsets `0x00`–`0x7E`) are **not** written by any command
on this page. `EEPROMWrite` (`0x41`, the general storage capability,
`src/caps/storage.c`) unconditionally refuses every address below `0x80`
regardless of build — see [`engineering.md`](engineering.md)'s "known gap"
note. There is currently no wire command that writes the device serial.
