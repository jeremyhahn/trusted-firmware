# Wire protocol — API reference

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

Every frame and every command, on every transport. Source of truth:
`include/ag/protocol.h`, `include/ag/status.h`, `src/core/commands.def`.

## Transports

There is one wire format and it is transport-agnostic: everything above L5
sees an `io.ReadWriter` (Go) / a byte stream (C), never a transport-specific
type.

| transport | carries the frame bytes unmodified | notes |
|---|---|---|
| UART, 8N1 | yes | `AG_UART_BAUD` default 115200; `serialport.Open` in the Go SDK |
| `agsim` stdio pipe | yes | real firmware compiled for the host; same codec, same bytes |

No second framing exists for either. Ethernet, when it lands, is a second
`Link` carrying the same frames — see `../host-sdk/architecture.md`.

## The frame

```
 off  size  field
  0    1    SOF      0xA5
  1    1    LEN      2 + payload_len (covers SEQ, OP and the payload; +1 if STATUS present)
  2    1    SEQ      request token, echoed verbatim in the response
  3    1    OP       request  CMD        0x00..0x7F
                     response CMD|0x80   0x80..0xFE
                     event    0xFF
  4    1    STATUS   responses and events only; absent from a request
  5+   N    PAYLOAD
  ..   2    CRC16    little-endian, over bytes [1 .. end of body]
```

Total on the wire is `LEN + 4`. `LEN` bounds: `AG_LEN_MIN` (2) to
`AG_MAX_LEN` (`AG_MAX_PAYLOAD + 3`, default 67). CRC-16/CCITT-FALSE, poly
`0x1021`, init `0xFFFF`, check value `0x29B1`.

`SEQ = AG_SEQ_NONE` (0) means the sender wants no correlation; the device
still echoes it.

## Discovery: `CAPS` (0x00)

Request: 0 bytes (board header) or 1 byte (pin index).

**Board header** response, 14 bytes, extended to 16:

```
off  size  field
 0    1    proto major
 1    1    proto minor
 2    1    firmware major
 3    1    firmware minor
 4    1    firmware patch
 5    1    board id
 6    1    pin count
 7    1    sched slots
 8    1    max payload
 9    1    crypto tier      0 none, 1 software, 2 secure-element, 3 tpm2-discrete
10    4    feature bitmap, little-endian uint32 (below)
14    2    algorithm bitmap, little-endian uint16 (below) -- APPENDED; absent from older firmware
```

Feature bitmap bits (LSB first): GPIO, SCHED, ANALOG, PWM, TONE, PULSE,
EEPROM, I2C, SPI, ONEWIRE, ATTEST, DEADMAN, PROVISION, ALIAS_CERT, AUTH_SESSION.

Algorithm bitmap: `0x0001` ECDSA P-256/SHA-256, `0x0002` Ed25519, `0x0004`
HMAC-SHA256. **Not** the same numbering as the `AlgID` byte inside an
attestation frame (`0x01`, `0x02`, `0x10` — sparse, to leave room below
`0x10` for future asymmetric algorithms).

**Pin descriptor** response (request carries a one-byte pin index), 5 bytes:

```
off  size  field
 0    1    PIN        echoed
 1    1    FLAGS      DIGITAL 0x01, PWM 0x02, ADC 0x04, RESERVED 0x20, IN_USE 0x40, LED 0x80
 2    1    PORT_BIT   port in the high nibble, bit in the low; high nibble 0x0F = no digital port
 3    1    ADC_CH     ADC channel, or 0xFF
 4    1    PWM        0 no compare unit; 1 available; 2 spent on the millisecond tick
```

Both major and minor of the reported proto version are checked by a host: a
major mismatch is refused (`ProtocolVersionError` in the Go SDK), minor is
tolerated in either direction.

## Command table

`id` — `name` — request payload — response payload. `min`/`max` are the
byte bounds `ag_dispatch` enforces from `commands.def` before a handler
runs; where request shape has two forms (mint vs. page) both are listed.

| id | name | build gate | request | response |
|---|---|---|---|---|
| 0x00 | `CAPS` | always | `[]` or `[pin:1]` | board header or pin descriptor, above |
| 0x01 | `PING` | always | `bytes[0..MaxPayload]` | the same bytes, echoed |
| 0x02 | `RESET` | always | `[mode:1]` (0 soft, 1 bootloader) | none; device acks, drains, then restarts. Mode 1 is not yet a distinct bootloader entry (it takes the same reset as mode 0) and is not gated by the auth session; issue #28 makes it a real path and adds the gate |
| 0x03 | `SYS_INFO` | always | `[]` | `[uptime_ms:4][free_ram:2][reset_cause:1][err_crc:2][err_format:2]` -- the last four bytes are appended (#5); a host that knows only the first seven still parses correctly |
| 0x04 | `WDT_SET` | **reserved, no handler in any build** | — | `AG_ERR_UNKNOWN_CMD` |
| 0x05 | `SLEEP` | **reserved, no handler in any build** | — | `AG_ERR_UNKNOWN_CMD` |
| 0x10 | `PIN_MODE` | `AG_FEATURE_GPIO` | `[pin:1][mode:1]` (0 in, 1 out, 2 input-pullup) | none |
| 0x11 | `DIGITAL_READ` | `AG_FEATURE_GPIO` | `[pin:1]` | `[level:1]` |
| 0x12 | `DIGITAL_WRITE` | `AG_FEATURE_GPIO` | `[pin:1][level:1]` | none |
| 0x14 | `TIMER_SET` | `AG_FEATURE_SCHED` | `[pin:1][level:1][ms:4]` | `[slot:1]` |
| 0x15 | `TIMER_CANCEL` | `AG_FEATURE_SCHED` | `[pin:1][apply_final:1]` | none |
| 0x16 | `TIMER_QUERY` | `AG_FEATURE_SCHED` | `[pin:1]` | `[active:1][held_level:1][remaining_ms:4]` |
| 0x20 | `ANALOG_READ` | `AG_FEATURE_ANALOG` | `[pin:1]` | `[raw:2]` (0..1023, uncalibrated) |
| 0x21 | `ADC_REF` | `AG_FEATURE_ANALOG` | `[ref:1]` (0 VCC, 1 external, 2 internal) | none |
| 0x22 | `PWM_WRITE` | `AG_FEATURE_PWM` | `[pin:1][duty:1]` | none |
| 0x23 | `TONE` | `AG_FEATURE_TONE` | `[pin:1][hz:2]` | none |
| 0x24 | `NO_TONE` | `AG_FEATURE_TONE` | `[]` or `[pin:1]` | none |
| 0x30 | `PULSE_IN` | `AG_FEATURE_PULSE` | `[pin:1][level:1][timeout_us:4]` | `[duration_us:4]` |
| 0x31 | `PULSE_COUNT_START` | `AG_FEATURE_PULSE` (defined, no table entry) | — | `AG_ERR_UNKNOWN_CMD` |
| 0x32 | `PULSE_COUNT_READ` | `AG_FEATURE_PULSE` (defined, no table entry) | — | `AG_ERR_UNKNOWN_CMD` |
| 0x33 | `PULSE_COUNT_STOP` | `AG_FEATURE_PULSE` (defined, no table entry) | — | `AG_ERR_UNKNOWN_CMD` |
| 0x34 | `PULSE_CAPTURE` | `AG_FEATURE_PULSE` | `[pin:1][start_high:1][max_edges:1][timeout_us:4]` | `[count:1][interval_us:2]*count` |
| 0x40 | `EEPROM_READ` | `AG_FEATURE_EEPROM` | `[addr:2][len:1]` | `bytes[len]`; any byte below `EEPROMReservedEnd` (0xC0) additionally requires an authenticated session on a tier that enforces one (`AG_FEATURE_AUTH_SESSION`) — `AG_ERR_AUTH_REQUIRED` otherwise, unconditional on other tiers |
| 0x41 | `EEPROM_WRITE` | `AG_FEATURE_EEPROM` | `[addr:2]bytes[1..8]` (chunk-limited) | none; any byte below `EEPROMReservedEnd` (0xC0) is `AG_ERR_PROTECTED_REGION`, unconditionally on every tier |
| 0x50 | `I2C_XFER` | `AG_FEATURE_I2C` | `[addr7:1][read_len:1]bytes[write...]` | `bytes[read_len]`; `addr7 == 0x60` (`AG_I2C_RESERVED_ADDR`) is `AG_ERR_PROTECTED_REGION` |
| 0x51 | `SPI_XFER` | `AG_FEATURE_SPI` | `[cs:1]bytes[write...]` | `bytes[len(write)]`, full duplex, CS held throughout |
| 0x52 | `ONEWIRE_RESET` | `AG_FEATURE_ONEWIRE` | `[pin:1]` | `[presence:1]` |
| 0x53 | `ONEWIRE_WRITE` | `AG_FEATURE_ONEWIRE` | `[pin:1]bytes[data...]` | none |
| 0x54 | `ONEWIRE_READ` | `AG_FEATURE_ONEWIRE` | `[pin:1][n:1]` | `bytes[n]` |
| 0x55 | `I2C_CONFIG` | `AG_FEATURE_I2C` | `[hz:4]` | none; `hz` outside `AG_I2C_HZ_MIN..AG_I2C_HZ_MAX` (1 kHz..400 kHz) is `AG_ERR_RANGE`; on a secure-element build, EVERY request is `AG_ERR_PROTECTED_REGION`, checked first |
| 0x56 | `SPI_CONFIG` | `AG_FEATURE_SPI` | `[mode:1][clkdiv:1][lsb_first:1]` | none; `mode` or `clkdiv` above 3, or `lsb_first` above 1, is `AG_ERR_RANGE` |
| 0x60 | `ATTEST` | `AG_FEATURE_ATTEST` | `[nonce:16]` (mint) or `[page:1]` | `[page:1][total:1][bytes:0..48]` — evidence is 164 bytes total, paged |
| 0x61 | `AUTH_CHALLENGE` | `AG_FEATURE_AUTH_SESSION` (element tier only) | `[]` | `[nonce:16]`, single-use |
| 0x62 | `AUTH_RESPONSE` | `AG_FEATURE_AUTH_SESSION` (element tier only) | `[sig:64]` — P-256 over `domain\|\|device_serial\|\|nonce` under the operator public key | none; grants a session lasting until reset |
| 0x63 | `ATTEST_CERT` | `AG_FEATURE_ALIAS_CERT` | `[nonce:32]` (mint) or `[page:1]` | `[page:1][total:2 LE][bytes:0..60]` — DER cert, 470-472 bytes total, paged |
| 0x68 | `OTA_BEGIN` | **ag-boot only** | `[total_len:4]` | in the application: `AG_ERR_UNKNOWN_CMD` |
| 0x69 | `OTA_DATA` | **ag-boot only** | `[offset:4]bytes[1..]` | in the application: `AG_ERR_UNKNOWN_CMD` |
| 0x6A | `OTA_END` | **ag-boot only** | `[]` | in the application: `AG_ERR_UNKNOWN_CMD` |
| 0x6B | `OTA_STATUS` | **ag-boot only** | `[]` | `[state:1][received:4]`; in the application: `AG_ERR_UNKNOWN_CMD` |
| 0x70 | `PROV_INFO` | `AG_FEATURE_PROVISION` (jig only) | `[]` | `[revision:4][config_block0:32][lock_value:1][lock_config:1]`, 38 bytes |
| 0x71 | `PROV_WRITE_CONFIG` | `AG_FEATURE_PROVISION` (jig only) | `[block:1]bytes[32]` | none; blocks 0-1 refused as `AG_ERR_PROTECTED_REGION` |
| 0x72 | `PROV_LOCK` | `AG_FEATURE_PROVISION` (jig only) | `[zone:1][crc:2]` | none; irreversible, and the CRC over the zone's expected contents is required nonzero |
| 0x73 | `PROV_GENKEY` | `AG_FEATURE_PROVISION` (jig only) | `[slot:1]` (must be the DeviceID slot) | `bytes[64]`, the public point; private key never leaves the element |
| 0x74 | `PROV_PUBKEY` | `AG_FEATURE_PROVISION` (jig only) | `[slot:1]` | `bytes[64]`, the public point recomputed, not regenerated |
| 0x75 | `PROV_SEED_UDS` | `AG_FEATURE_PROVISION` (jig only) | `[slot:1]` (must be the UDS slot) | none; UDS crosses the element's internal bus only, never the serial link |

Every peripheral the HAL exports an init for is initialised at boot with a
sane default — `AG_I2C_DEFAULT_HZ` (100 kHz), `AG_SPI_DEFAULT_MODE`/`CLKDIV`/
`LSB_FIRST` (mode 0, slowest, MSB-first) — so `I2C_XFER` and `SPI_XFER` work
before a host ever sends `I2C_CONFIG` or `SPI_CONFIG`. Those two exist to let
a host ask for something else once it knows what is on the bus, not to make
the bus usable in the first place; see `src/main.c` and `src/caps/bus.c`.

`OTA_*` and `PROV_*` ids appear in no build's `commands.def` entry for the
application image — never a mode of `ag_dispatch`, on purpose. `OTA_*` is
answered only by `ag-boot`; an application build must answer
`AG_ERR_UNKNOWN_CMD`, which tells a host to reset into the bootloader.
`PROV_*` is present only in a provisioning-jig build
(`AG_FEATURE_PROVISION`, default 0); a fielded device has no handler at all.
`WDT_SET`, `SLEEP` and `PULSE_COUNT_*` are defined ids with **no handler in
any current build** — reserved, and refused as `AG_ERR_UNKNOWN_CMD`
everywhere today; see `include/ag/protocol.h` for the one-line note against
each.

`AUTH_CHALLENGE`/`AUTH_RESPONSE` (issue #4) are implemented, but **element
tier only** (`AG_FEATURE_AUTH_SESSION`, on for `CRYPTO=atecc608a` except where
a board profile says no — see `boards/atmega328p/board_profile.h`). A
soft-tier or `CRYPTO=none` image does not compile them in at all and answers
`AG_ERR_UNKNOWN_CMD`, the same as any other absent feature: the only
available software-tier verify primitive does not fit this device family
(`docs/security/threat-model.md`).

## Events

`OP = 0xFF`, `STATUS` present (always `AG_OK`), one payload byte: the event
id.

| id | name | meaning |
|---|---|---|
| 0x01 | `EVENT_BOOT` | sent once at startup; an unexpected one means the device reset |
| 0x02 | `EVENT_DEADMAN` | the link went quiet and scheduled pins were driven to safety |
| 0x03 | `EVENT_COUNTER_WRAP` | a pulse counter wrapped |

Emitted only when no response is in flight; see `engineering.md` invariants.

## Status codes

One byte, carried in every response and event.

| value | name | meaning |
|---|---|---|
| 0 | `AG_OK` | success |
| 1 | `AG_ERR_CRC` | frame CRC mismatch |
| 2 | `AG_ERR_FRAMING` | implausible LEN, or idle timeout mid-frame |
| 3 | `AG_ERR_TOO_LONG` | request exceeds `AG_MAX_LEN`, or a response overflowed |
| 4 | `AG_ERR_UNKNOWN_CMD` | no table entry for this command id |
| 5 | `AG_ERR_BAD_ARGC` | payload length wrong for this command |
| 6 | `AG_ERR_BAD_PIN` | pin index out of range for this board |
| 7 | `AG_ERR_UNSUPPORTED` | compiled out, or this pin lacks the capability |
| 8 | `AG_ERR_BUSY` | peripheral already in use |
| 9 | `AG_ERR_TIMEOUT` | `PULSE_IN`, I2C, SPI or 1-Wire timed out |
| 10 | `AG_ERR_NACK` | I2C address or data NACK |
| 11 | `AG_ERR_RANGE` | value outside the permitted interval |
| 12 | `AG_ERR_NO_SLOT` | scheduler or counter table full |
| 13 | `AG_ERR_RESERVED_PIN` | pin is `/RESET`, or claimed by the soft UART |
| 14 | `AG_ERR_CRYPTO` | the crypto provider failed the operation |
| 15 | `AG_ERR_NOT_PROVISIONED` | no device identity has been provisioned |
| 16 | `AG_ERR_PROTECTED_REGION` | the target is platform-reserved |
| 17 | `AG_ERR_AUTH_REQUIRED` | command needs an authenticated session |
| 18 | `AG_ERR_INTERNAL` | a condition the firmware believes impossible |

`AG_STATUS_MAX` = 0xFE; values extend upward and never renumber.

## Attestation evidence layout (carried inside `ATTEST`'s paged payload)

```
off  size  field
 0    1    Version
 1   16    Nonce
17   16    DeviceID
33   32    FWHash
65    4    BootCount
69    4    UptimeMs
73    1    AlgID       0x01 ECDSA P-256/SHA-256, 0x02 Ed25519, 0x10 HMAC-SHA256
74   64    Signature
138  26    Reserved
```

164 bytes total (`AG_ATTEST_LEN`), paged in 48-byte windows
(`AG_ATTEST_PAGE`). `Timestamp` is `UptimeMs`, never wall-clock — no part in
this family has an RTC, and freshness is always the verifier's nonce.
