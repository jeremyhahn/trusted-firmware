# Wire protocol — API reference

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

Every frame and every command, on every transport. Source of truth:
`include/tfw/protocol.h`, `include/tfw/status.h`, `src/core/commands.def`.

## Transports

There is one wire format and it is transport-agnostic: everything above L5
sees an `io.ReadWriter` (Go) / a byte stream (C), never a transport-specific
type.

| transport | carries the frame bytes unmodified | notes |
|---|---|---|
| UART, 8N1 | yes | `TFW_UART_BAUD` default 115200; `serialport.Open` in the Go SDK |
| TCP over WIZnet W5100/W5500 | yes | the device opens the connection; allow-listed commands only — see `../ethernet-transport/` |
| `tfwsim` stdio pipe | yes | real firmware compiled for the host; same codec, same bytes |

No second framing exists for any of them. Ethernet is a second `Link`
carrying the same frames — see `../ethernet-transport/architecture.md` and
`../host-sdk/architecture.md`.

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

Total on the wire is `LEN + 4`. `LEN` bounds: `TFW_LEN_MIN` (2) to
`TFW_MAX_LEN` (`TFW_MAX_PAYLOAD + 3`, default 67). CRC-16/CCITT-FALSE, poly
`0x1021`, init `0xFFFF`, check value `0x29B1`.

`SEQ = TFW_SEQ_NONE` (0) means the sender wants no correlation; the device
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
EEPROM, I2C, SPI, ONEWIRE, ATTEST, DEADMAN, PROVISION, ALIAS_CERT, AUTH_SESSION,
ATTEST_CSR.

Algorithm bitmap: `0x0001` ECDSA P-256/SHA-256, `0x0002` Ed25519, `0x0004`
HMAC-SHA256. **Not** the same numbering as the `AlgID` byte inside an
attestation frame (`0x01`, `0x02`, `0x10` — sparse, to leave room below
`0x10` for future asymmetric algorithms).

**Pin descriptor** response (request carries a one-byte pin index), 6 bytes
(#12; was 5, with port and bit packed into one byte as two nibbles):

```
off  size  field
 0    1    PIN        echoed
 1    1    FLAGS      DIGITAL 0x01, PWM 0x02, ADC 0x04, RESERVED 0x20, IN_USE 0x40, LED 0x80
 2    1    PORT       port index; 0xFF = no digital port
 3    1    BIT        bit within the port, 0..255; 0xFF when PORT is 0xFF
 4    1    ADC_CH     ADC channel, or 0xFF
 5    1    PWM        0 no compare unit; 1 available; 2 spent on the millisecond tick
```

Port and bit are a whole byte each so a part with 32-bit ports (Cortex-M) fits.
How the board table stores a pin's coordinate is the architecture's
(`tfw_pin_coord_t` in `src/hal/<arch>/hal_types.h`): the AVR keeps one byte of
two nibbles, since its ports and bits fit, and the host build (`tfwsim`) stores
a whole byte of each. Core reads the coordinate only through the architecture's
accessors, so the page carries whatever the architecture stores, untruncated.

The board id (header offset 5) is allocated per architecture family:
[`../boards/board-id-registry.md`](../boards/board-id-registry.md).

Both major and minor of the reported proto version are checked by a host: a
major mismatch is refused (`ProtocolVersionError` in the Go SDK), minor is
tolerated in either direction.

The current version is **2.0**. History:

| version | change | wire bytes |
|---|---|---|
| 1.0 | initial | header bytes 0-1 `01 00`; five-byte pin page `[pin][flags][port<<4\|bit][adc][pwm]` |
| 2.0 (#12) | six-byte pin page, port and bit a whole byte each | header bytes 0-1 `02 00`; pin-page byte 2 is the port alone, byte 3 the bit, ADC moves from byte 3 to 4, PWM state from 4 to 5; "no port" is `FF FF` (was a high nibble of `0x0F`) |

A 1.x host reading a 2.0 pin page would take the bit for the ADC channel and
the ADC channel for the PWM state, which is why this is a major and not a
minor: a 2.x host and 1.x firmware, or the reverse, refuse each other at CAPS.
Pinned as `TFWV_CAPS_PROTO_REPLY` and `TFWV_CAPS_PIN13_REPLY` in
`testdata/vectors.h` and their Go mirrors.

## Command table

`id` — `name` — request payload — response payload. `min`/`max` are the
byte bounds `tfw_dispatch` enforces from `commands.def` before a handler
runs; where request shape has two forms (mint vs. page) both are listed.

| id | name | build gate | request | response |
|---|---|---|---|---|
| 0x00 | `CAPS` | always | `[]` or `[pin:1]` | board header or pin descriptor, above |
| 0x01 | `PING` | always | `bytes[0..MaxPayload]` | the same bytes, echoed |
| 0x02 | `RESET` | always | `[mode:1]` (0 soft, 1 bootloader) | none; device acks, drains, then restarts. Mode 0 restarts the application. Mode 1 restarts into tfw-boot's OTA session (see below). Mode 1 requires the operator auth session (`TFW_ERR_AUTH_REQUIRED` without one) on the element tier; soft and `CRYPTO=none` builds link the permissive gate and need none. On a board with no boot section (ATtiny85) mode 1 is refused with `TFW_ERR_UNSUPPORTED` and nothing resets, so the host is told rather than silently handed the application. Any other mode is `TFW_ERR_RANGE` |
| 0x03 | `SYS_INFO` | always | `[]` | `[uptime_ms:4][free_ram:2][reset_cause:1][err_crc:2][err_format:2][free_ram32:4]` -- the last four bytes of the counters are appended (#5) and `free_ram32` after them (#12); a host that knows only the first seven still parses correctly. `free_ram` saturates at 0xFFFF; `free_ram32` is exact and wins when present |
| 0x04 | `WDT_SET` | **reserved, no handler in any build** | — | `TFW_ERR_UNKNOWN_CMD` |
| 0x05 | `SLEEP` | **reserved, no handler in any build** | — | `TFW_ERR_UNKNOWN_CMD` |
| 0x06 | `APP_INFO` | `TFW_FEATURE_APP` (images built with an `APP_DIR` only) | `[]` | `[state:1][overruns:2][abi:1][ota_slots:1][major:1][minor:1][patch:2][config_len:2][required_caps:4][name:16][profile:32]`, 63 bytes — the runner and the application descriptor as written, in every state; see [application-sdk/api.md](../application-sdk/api.md#app_info) |
| 0x10 | `PIN_MODE` | `TFW_FEATURE_GPIO` | `[pin:1][mode:1]` (0 in, 1 out, 2 input-pullup) | none |
| 0x11 | `DIGITAL_READ` | `TFW_FEATURE_GPIO` | `[pin:1]` | `[level:1]` |
| 0x12 | `DIGITAL_WRITE` | `TFW_FEATURE_GPIO` | `[pin:1][level:1]` | none |
| 0x14 | `TIMER_SET` | `TFW_FEATURE_SCHED` | `[pin:1][level:1][ms:4]` | `[slot:1]` |
| 0x15 | `TIMER_CANCEL` | `TFW_FEATURE_SCHED` | `[pin:1][apply_final:1]` | none |
| 0x16 | `TIMER_QUERY` | `TFW_FEATURE_SCHED` | `[pin:1]` | `[active:1][held_level:1][remaining_ms:4]` |
| 0x20 | `ANALOG_READ` | `TFW_FEATURE_ANALOG` | `[pin:1]` | `[raw:2]` (0..1023, uncalibrated) |
| 0x21 | `ADC_REF` | `TFW_FEATURE_ANALOG` | `[ref:1]` (0 VCC, 1 external, 2 internal) | none |
| 0x22 | `PWM_WRITE` | `TFW_FEATURE_PWM` | `[pin:1][duty:1]` | none |
| 0x23 | `TONE` | `TFW_FEATURE_TONE` | `[pin:1][hz:2]` | none |
| 0x24 | `NO_TONE` | `TFW_FEATURE_TONE` | `[]` or `[pin:1]` | none |
| 0x30 | `PULSE_IN` | `TFW_FEATURE_PULSE` | `[pin:1][level:1][timeout_us:4]` | `[duration_us:4]` |
| 0x31 | `PULSE_COUNT_START` | `TFW_FEATURE_PULSE` (defined, no table entry) | — | `TFW_ERR_UNKNOWN_CMD` |
| 0x32 | `PULSE_COUNT_READ` | `TFW_FEATURE_PULSE` (defined, no table entry) | — | `TFW_ERR_UNKNOWN_CMD` |
| 0x33 | `PULSE_COUNT_STOP` | `TFW_FEATURE_PULSE` (defined, no table entry) | — | `TFW_ERR_UNKNOWN_CMD` |
| 0x34 | `PULSE_CAPTURE` | `TFW_FEATURE_PULSE` | `[pin:1][start_high:1][max_edges:1][timeout_us:4]` | `[count:1][interval_us:2]*count` |
| 0x40 | `EEPROM_READ` | `TFW_FEATURE_EEPROM` | `[addr:2][len:1]` | `bytes[len]`; any byte below `EEPROMReservedEnd` (0xC0) additionally requires an authenticated session on a tier that enforces one (`TFW_FEATURE_AUTH_SESSION`) — `TFW_ERR_AUTH_REQUIRED` otherwise, unconditional on other tiers |
| 0x41 | `EEPROM_WRITE` | `TFW_FEATURE_EEPROM` | `[addr:2]bytes[1..8]` (chunk-limited) | none; any byte below `EEPROMReservedEnd` (0xC0) is `TFW_ERR_PROTECTED_REGION`, unconditionally on every tier |
| 0x50 | `I2C_XFER` | `TFW_FEATURE_I2C` | `[addr7:1][read_len:1]bytes[write...]` | `bytes[read_len]`; `addr7 == 0x60` (`TFW_I2C_RESERVED_ADDR`) or the board profile's element address (`0x36` on the Nano and Mega), or `0x30` on a build that links the OPTIGA Trust M transport, is `TFW_ERR_PROTECTED_REGION` |
| 0x51 | `SPI_XFER` | `TFW_FEATURE_SPI` | `[cs:1]bytes[write...]` | `bytes[len(write)]`, full duplex, CS held throughout |
| 0x52 | `ONEWIRE_RESET` | `TFW_FEATURE_ONEWIRE` | `[pin:1]` | `[presence:1]` |
| 0x53 | `ONEWIRE_WRITE` | `TFW_FEATURE_ONEWIRE` | `[pin:1]bytes[data...]` | none |
| 0x54 | `ONEWIRE_READ` | `TFW_FEATURE_ONEWIRE` | `[pin:1][n:1]` | `bytes[n]` |
| 0x55 | `I2C_CONFIG` | `TFW_FEATURE_I2C` | `[hz:4]` | none; `hz` outside `TFW_I2C_HZ_MIN..TFW_I2C_HZ_MAX` (1 kHz..400 kHz) is `TFW_ERR_RANGE`; on a secure-element build, EVERY request is `TFW_ERR_PROTECTED_REGION`, checked first |
| 0x56 | `SPI_CONFIG` | `TFW_FEATURE_SPI` | `[mode:1][clkdiv:1][lsb_first:1]` | none; `mode` or `clkdiv` above 3, or `lsb_first` above 1, is `TFW_ERR_RANGE` |
| 0x60 | `ATTEST` | `TFW_FEATURE_ATTEST` | `[nonce:16]` (mint) or `[page:1]` | `[page:1][total:1][bytes:0..48]` — evidence is 164 bytes total, paged |
| 0x61 | `AUTH_CHALLENGE` | `TFW_FEATURE_AUTH_SESSION` (element tier only) | `[]` | `[nonce:16]`, single-use |
| 0x62 | `AUTH_RESPONSE` | `TFW_FEATURE_AUTH_SESSION` (element tier only) | `[sig:64]` — P-256 over `domain\|\|device_serial\|\|nonce` under the operator public key | none; grants a session lasting until reset |
| 0x63 | `ATTEST_CERT` | `TFW_FEATURE_ALIAS_CERT` | `[nonce:32]` (mint) or `[page:1]` | `[page:1][total:2 LE][bytes:0..60]` — DER cert, 471-473 bytes total, paged |
| 0x64 | `ATTEST_CSR` | `TFW_FEATURE_ATTEST_CSR` (ATmega2560 element tier only) | `[b:32]` | `[sig:64]` r‖s — the DeviceID key's P-256 signature over the 365-byte CertificationRequestInfo the device builds and the host rebuilds; nothing paged, nothing kept. Any other length is `TFW_ERR_BAD_ARGC` |
| 0x68 | `OTA_BEGIN` | **tfw-boot only** | `[total_len:4]` | in the application: `TFW_ERR_UNKNOWN_CMD` |
| 0x69 | `OTA_DATA` | **tfw-boot only** | `[offset:4]bytes[1..]` | in the application: `TFW_ERR_UNKNOWN_CMD` |
| 0x6A | `OTA_END` | **tfw-boot only** | `[]` | in the application: `TFW_ERR_UNKNOWN_CMD` |
| 0x6B | `OTA_STATUS` | **tfw-boot only** | `[]` | `[state:1][received:4]`; in the application: `TFW_ERR_UNKNOWN_CMD` |
| 0x70 | `PROV_INFO` | `TFW_FEATURE_PROVISION` (jig only) | `[]` | `[revision:4][config_block0:32][lock_value:1][lock_config:1]`, 38 bytes |
| 0x71 | `PROV_WRITE_CONFIG` | `TFW_FEATURE_PROVISION` (jig only) | `[block:1]bytes[32]` | none; blocks 0-1 refused as `TFW_ERR_PROTECTED_REGION` |
| 0x72 | `PROV_LOCK` | `TFW_FEATURE_PROVISION` (jig only) | `[zone:1][crc:2]` | none; irreversible, and the CRC over the zone's expected contents is required nonzero |
| 0x73 | `PROV_GENKEY` | `TFW_FEATURE_PROVISION` (jig only) | `[slot:1]` (must be the DeviceID slot) | `bytes[64]`, the public point; private key never leaves the element |
| 0x74 | `PROV_PUBKEY` | `TFW_FEATURE_PROVISION` (jig only) | `[slot:1]` | `bytes[64]`, the public point recomputed, not regenerated |
| 0x75 | `PROV_SEED_UDS` | `TFW_FEATURE_PROVISION` (jig only) | `[slot:1]` (must be the UDS slot) | none; UDS crosses the element's internal bus only, never the serial link |

Every peripheral the HAL exports an init for is initialised at boot with a
sane default — `TFW_I2C_DEFAULT_HZ` (100 kHz), `TFW_SPI_DEFAULT_MODE`/`CLKDIV`/
`LSB_FIRST` (mode 0, slowest, MSB-first) — so `I2C_XFER` and `SPI_XFER` work
before a host ever sends `I2C_CONFIG` or `SPI_CONFIG`. Those two exist to let
a host ask for something else once it knows what is on the bus, not to make
the bus usable in the first place; see `src/main.c` and `src/caps/bus.c`.

`OTA_*` and `PROV_*` ids appear in no build's `commands.def` entry for the
application image — never a mode of `tfw_dispatch`, on purpose. `OTA_*` is
answered only by `tfw-boot`; an application build must answer
`TFW_ERR_UNKNOWN_CMD`, which tells a host to reset into the bootloader.
`PROV_*` is present only in a provisioning-jig build
(`TFW_FEATURE_PROVISION`, default 0); a fielded device has no handler at all.
`WDT_SET`, `SLEEP` and `PULSE_COUNT_*` are defined ids with **no handler in
any current build** — reserved, and refused as `TFW_ERR_UNKNOWN_CMD`
everywhere today; see `include/tfw/protocol.h` for the one-line note against
each.
`0x7F` is never assigned: its response OP would be `0xFF`, the event marker.
Today a `0x7F` request is answered `TFW_ERR_UNKNOWN_CMD` in a frame whose OP
is `0xFF`, which a host reads as an event; whether the device should answer it
at all is open question 4 of
[adr-application-wire-commands.md](../application-sdk/adr-application-wire-commands.md).

`APP_INFO` (issue #51) is in the table only of an image built with an
application (`TFW_FEATURE_APP`, set by `APP_DIR`). Every other image answers
`TFW_ERR_UNKNOWN_CMD`, as it did before the id existed. It has no CAPS bit:
the command answering is the capability.

`AUTH_CHALLENGE`/`AUTH_RESPONSE` (issue #4) are implemented, but **element
tier only** (`TFW_FEATURE_AUTH_SESSION`, on for `CRYPTO=atecc608a` except where
a board profile says no — see `boards/atmega328p/board_profile.h`). A
soft-tier or `CRYPTO=none` image does not compile them in at all and answers
`TFW_ERR_UNKNOWN_CMD`, the same as any other absent feature: the only
available software-tier verify primitive does not fit this device family
(`docs/security/threat-model.md`).

`ATTEST_CSR` (issue #30) is built only beside `ATTEST_CERT`, so never on the
ATtiny85 or the ATmega32U4, and the ATmega328P's profile turns it off by name
because it puts that part's element image over its flash budget; the 32U4's
profile says so by name too (#6). The ATmega2560's
element tier is the one image that carries it; elsewhere 0x64 answers
`TFW_ERR_UNKNOWN_CMD` and CAPS bit 15 is clear. The
host sends 32 bytes and nothing else; the device fills a fixed PKCS#10
CertificationRequestInfo from its own key, UEID and measurement, puts `b`
in the evidence extension's nonce, and signs it. It names itself by the
lowercase hex of its full UEID (type byte + DeviceID, 34 digits, go-xkms#1441).
The host never sends a digest
and never sees the CRI: it rebuilds the same 365 bytes from the alias
certificate and `b`, and wraps them with the signature. The template and the
host's rebuild recipe are in
[../dice-attestation/api.md](../dice-attestation/api.md); the bytes are pinned
as `TFWV_CSR_CRI` in `testdata/vectors.h`. CAPS reports it as feature bit 15.

## RESET into the bootloader

`RESET` mode 1 on a healthy device takes a watchdog reset after leaving tfw-boot
a one-shot request (`include/tfw/boot.h`, "the update-session request"). tfw-boot
honours it only after a pure watchdog reset, clears it before doing anything
else, and opens its OTA session instead of jumping. What the host sees:

1. The `RESET` response (`TFW_OK`), then the reset.
2. An unsolicited `EVENT` from tfw-boot with status `TFW_OK` and **no payload**.
   The application's `EVENT_BOOT` has one payload byte, so the payload length
   tells the two apart; a refusal announcement from tfw-boot carries a non-`TFW_OK`
   status.
3. `PING` is answered; application commands return `TFW_ERR_UNKNOWN_CMD`; the
   `OTA_*` commands work ([../secure-boot/api.md](../secure-boot/api.md)).

The session ends on a verified `OTA_END` (tfw-boot hands over to the new image),
on `RESET` (the application starts), after 10 s of silence (or as many junk bytes as the line
can carry in 10 s) without an accepted `OTA_BEGIN`, `OTA_DATA` or `OTA_END`,
after an absolute ceiling of 60 s or about 83 KB (ATmega2560; 46 KB on the 28 KB
parts) that nothing restarts, or at the first `OTA_END` that does not verify --
one attempt per request. Each of the last three falls through to tfw-boot's
ordinary measure, verify and jump. `PING`, `OTA_STATUS` and rejected frames do
not extend anything. If the failed attempt had already overwritten application
pages, that fall-through fails verification and the device is in recovery
(unbounded, waiting for a good image): a single-slot layout has no other
outcome. Nothing is granted by the session: every
image is signature- and floor-checked exactly as at a cold boot. A `BOOT_OTA=0`
bootloader has no session loop and ignores the request, so mode 1 there is an
ordinary restart.

## Events

`OP = 0xFF`, `STATUS` present (always `TFW_OK`), one payload byte: the event
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
| 0 | `TFW_OK` | success |
| 1 | `TFW_ERR_CRC` | frame CRC mismatch |
| 2 | `TFW_ERR_FRAMING` | implausible LEN, or idle timeout mid-frame |
| 3 | `TFW_ERR_TOO_LONG` | request exceeds `TFW_MAX_LEN`, or a response overflowed |
| 4 | `TFW_ERR_UNKNOWN_CMD` | no table entry for this command id |
| 5 | `TFW_ERR_BAD_ARGC` | payload length wrong for this command |
| 6 | `TFW_ERR_BAD_PIN` | pin index out of range for this board |
| 7 | `TFW_ERR_UNSUPPORTED` | compiled out, or this pin lacks the capability |
| 8 | `TFW_ERR_BUSY` | peripheral already in use |
| 9 | `TFW_ERR_TIMEOUT` | `PULSE_IN`, I2C, SPI or 1-Wire timed out |
| 10 | `TFW_ERR_NACK` | I2C address or data NACK |
| 11 | `TFW_ERR_RANGE` | value outside the permitted interval |
| 12 | `TFW_ERR_NO_SLOT` | scheduler or counter table full |
| 13 | `TFW_ERR_RESERVED_PIN` | pin is `/RESET`, or claimed by the soft UART |
| 14 | `TFW_ERR_CRYPTO` | the crypto provider failed the operation |
| 15 | `TFW_ERR_NOT_PROVISIONED` | no device identity has been provisioned |
| 16 | `TFW_ERR_PROTECTED_REGION` | the target is platform-reserved |
| 17 | `TFW_ERR_AUTH_REQUIRED` | command needs an authenticated session |
| 18 | `TFW_ERR_INTERNAL` | a condition the firmware believes impossible |
| 19 | `TFW_ERR_BAD_STATE` | the command is not valid in the current OTA state |
| 20 | `TFW_ERR_NO_STORE` | the external store did not answer its JEDEC ID |

`TFW_STATUS_MAX` = 0xFE; values extend upward and never renumber.

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

164 bytes total (`TFW_ATTEST_LEN`), paged in 48-byte windows
(`TFW_ATTEST_PAGE`). `Timestamp` is `UptimeMs`, never wall-clock — no part in
this family has an RTC, and freshness is always the verifier's nonce.
