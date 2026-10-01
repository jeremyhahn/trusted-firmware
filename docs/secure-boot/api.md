# ag-boot — API

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

One transport: the framed UART protocol (`src/proto/frame.c`), polled, no
interrupts. `ag-boot` has no REST, gRPC, QUIC, MCP or Unix-socket surface —
this repo owns the AVR wire protocol and nothing above it (see the top-level
`CLAUDE.md`). Requests and responses use the same frame codec the application
uses, which is the whole reason OTA frames and application frames cannot
drift apart. Definitions are `include/ag/boot.h`, `include/ag/protocol.h` and
`include/ag/status.h`.

## Commands

Handled by `ag_boot_session_handle` (`boot/boot_session.c`), reachable only
when the bootloader is in its OTA recovery path
(`boot/recovery/recovery_ota.c`, linked when `BOOT_OTA=1`). Every other
command — the application's full set — returns `AG_ERR_UNKNOWN_CMD`, which is
how a host distinguishes "talking to the bootloader" from "talking to a dead
device."

| Command | ID | Request payload | Response payload | Notes |
|---|---|---|---|---|
| `AG_CMD_PING` | `0x01` | 0–N bytes, echoed | same bytes | the only command answered identically by both images; used to tell "in the bootloader, waiting" from "dead" |
| `AG_CMD_RESET` | `0x02` | `[mode:1]` | none | `AG_BOOT_SESSION_RESET`; the loop calls `ag_boot_reset()` after the response frame is sent |
| `AG_CMD_OTA_BEGIN` | `0x68` | `[total_len:4]` | none | declares the whole image length, trailer included; refuses a length that is not a whole page count or does not reach `AG_BOOT_TRAILER_LEN`. No version argument — the version lives inside the trailer, under the signature |
| `AG_CMD_OTA_DATA` | `0x69` | `[offset:4][bytes:1..]` | none | `offset` must match the session's `next_offset` exactly; a chunk stream with no offset would let one dropped frame silently shift everything after it |
| `AG_CMD_OTA_END` | `0x6A` | none | none | refuses unless exactly `declared_len` bytes arrived; flushes a partial final page padded with `0xFF`. Success here means every byte arrived — **nothing has been verified**. The session action is `AG_BOOT_SESSION_COMMIT`, which makes the recovery loop run `ag_boot_evaluate` next |
| `AG_CMD_OTA_STATUS` | `0x6B` | none | `[state:1][received:4]` | `state` is `ag_ota_state_t`: `AG_OTA_IDLE`(0) / `RECEIVING`(1) / `COMPLETE`(2) / `FAILED`(3) |

Status byte in every response frame is `ag_status_t`. The ones this surface
can return: `AG_OK`(0), `AG_ERR_BAD_ARGC`(5, wrong payload length for the
command), `AG_ERR_RANGE`(11, a length or offset outside what's possible),
`AG_ERR_PROTECTED_REGION`(16, an OTA write reaching the boot section),
`AG_ERR_UNKNOWN_CMD`(4, any application command). `OTA_END`'s
`AG_BOOT_SESSION_COMMIT` triggers a **second**, unsolicited `EVENT` frame
after the response — the result of `ag_boot_evaluate` on the just-received
image — carrying `AG_ERR_PROTECTED_REGION` (downgrade), `AG_ERR_CRYPTO`
(measurement or signature failed), `AG_ERR_NOT_PROVISIONED` (no usable key),
or `AG_OK` before the device jumps.

`ag-boot` also emits one unsolicited `EVENT` frame on entry to recovery,
carrying the `ag_status_t` reason it refused to boot — the same set
`ag_boot_evaluate` can return (see [engineering.md](engineering.md)'s error
table).

## Structures

All three are defined once in `include/ag/boot.h` because each has a second
party that must agree byte for byte, and every field is read by shift and
mask — never a struct cast — so no compiler's padding rules become part of
the wire contract.

### The image trailer

The last `AG_BOOT_TRAILER_LEN` (128) bytes of the application region, at
`AG_BOOT_TRAILER_ADDR = AG_BOOT_APP_END - 128`:

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | `magic` | `AG_BOOT_TRAILER_MAGIC` = `0x4D494741` |
| 4 | 2 | `trailer_version` | `AG_BOOT_TRAILER_VERSION` = 1, exact match required |
| 6 | 2 | reserved | |
| 8 | 4 | `image_version` | the anti-rollback security version number; 0 is refused |
| 12 | 4 | `image_len` | bytes measured, from address 0; bounded by `AG_BOOT_MAX_IMAGE_LEN` |
| 16 | 32 | `measurement` | SHA-256 over `[0, image_len)` — this is `FWHash` |
| — | — | *signed span ends here* | bytes `[0, 48)`, `AG_BOOT_TR_SIGNED_LEN` |
| 48 | 64 | `signature` | Ed25519 (or the element's ECDSA-backed verify) over the signed span |
| 112 | 16 | reserved | |

Parsed by `ag_boot_trailer_parse`, serialised by `ag_boot_trailer_serialise`
(`boot/boot_image.c`). The build-time signing tool and the OTA-sending host
tool are the two other parties.

### Reserved EEPROM

`AG_BOOT_EE_BASE` (`0x000`) through `0x0BF`, `AG_BOOT_EE_LEN` (192) bytes,
refused for application writes by `src/caps/storage.c`:

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | `magic` | `AG_BOOT_EE_MAGIC` = `0x52504741` |
| 4 | 1 | `layout_version` | `AG_BOOT_EE_VERSION` = 2 (grew from 1 when `operator_pubkey` below was added — issue #4; an older-layout region parses as unprovisioned rather than partially) |
| 5 | 1 | `flags` | |
| 6 | 2 | reserved | |
| 8 | 32 | `pubkey_hash` | SHA-256 of the 32-byte Ed25519 public key; ignored on the element tier |
| 40 | 4 | `rollback_floor` | software tier only; the element tier uses its own monotonic counter |
| 44 | 4 | `boot_count` | |
| 48 | 1 | `boot_state` | `AG_BOOT_STATE_EMPTY`(0) / `STAGED`(1) / `GOOD`(2) |
| 49 | 1 | `ota_state` | `AG_BOOT_OTA_IDLE`(0) / `RECEIVING`(1) / `VERIFIED`(2) / `FAILED`(3) |
| 50 | 2 | `boot_fail_count` | |
| 52 | 16 | `device_serial` | baked into the `.eep` image by `agprov --bake-eeprom` before the part is ever flashed — there is no wire command that writes it |
| 68 | 64 | `operator_pubkey` | the `AUTH_CHALLENGE`/`AUTH_RESPONSE` session's trust anchor (issue #4), P-256 as X‖Y; element tier only, baked in the same way as `device_serial`; see [../security/threat-model.md](../security/threat-model.md) |
| 132 | 58 | reserved | |
| 190 | 2 | `crc16` | over bytes `[0, 190)` — corruption detection, **not** integrity against an attacker; anyone who can write the region can recompute it |

Parsed by `ag_boot_platform_parse`, serialised by `ag_boot_platform_serialise`
(`boot/boot_platform.c`); accessed through `ag_boot_platform_load` /
`ag_boot_platform_store` (`boot/boot_store.c`). The second party is `agprov`
(`sdk/go/cmd/agprov`) — see
[../device-provisioning/user.md](../device-provisioning/user.md).

### The DICE CDI handoff record

`AG_BOOT_HANDOFF_LEN` (80) bytes of SRAM in its own `.handoff` section --
uninitialised-but-not-zeroed storage, the same idea `.noinit` names, but its
own section rather than that one: `src/hal/avr/hal_early.c`'s
`ag_hal_early_flags` lives in ordinary `.noinit` for the identical reason
(surviving past `.init3`, before `.bss` is cleared), and pinning the handoff
block's address onto the generic `.noinit` output section would carry that
variable along with it — briefly true of this codebase and the reason this
note is here. At the top of RAM, written once by `ag_boot_handoff_write` and
read once by the application (`src/core/handoff.c`'s `ag_boot_handoff_init`,
at startup, before `ag_crypto_init`):

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | `magic` | `AG_BOOT_HANDOFF_MAGIC` = `0x49444341` |
| 4 | 1 | `version` | `AG_BOOT_HANDOFF_VERSION` = 2 |
| 5 | 1 | `tier` | the `ag_crypto_tier_t` `ag_boot_cdi_tier()` observed |
| 6 | 2 | `flags` | |
| 8 | 32 | `cdi` | `HMAC(UDS, measurement)` |
| 40 | 32 | `measurement` | so the application reports the `FWHash` actually verified, not a recomputation |
| 72 | 4 | `image_version` | |
| 76 | 1 | `reset_cause` | raw `MCUSR`, latched by `hal_early.c` before ag-boot's own `.init3` clears it — issue #18 |
| 77 | 1 | reserved | |
| 78 | 2 | `crc16` | over bytes `[0, 78)` |

Version 2 repurposed bytes rather than growing the block: version 1 held
`crc16` at offset 76 and two reserved bytes at 78; version 2 moved `crc16` to
the end of what it now covers, widening coverage to include `reset_cause`.
`AG_BOOT_HANDOFF_LEN` is unchanged.

Built by `ag_boot_handoff_build`, parsed by `ag_boot_handoff_parse`
(`boot/boot_handoff.c`) — pure functions over the byte layout, testable
without an MCU on either side. `ag_boot_handoff_parse` returns
`AG_ERR_NOT_PROVISIONED` on a missing magic, `AG_ERR_UNSUPPORTED` on a version
this build doesn't know, and `AG_ERR_CRC` on a block a stack overwrote — the
one failure worth naming on a part with no MPU. An unknown or older version
degrades exactly like a missing handoff: `DeviceID`/`FWHash` stay zero and
`ag_hal_reset_cause()` falls back to the application's own `.init3`
observation — never a block misread as something it is not.

**This is data, never a call.** The application asking the bootloader for its
CDI through a trampoline is exactly what the tier forbids — see
[architecture.md](architecture.md)'s trust boundaries.

## Versioning

Every layout above is additive-only: offsets never move, fields never
renumber, and a `*_version`/`layout_version` byte gates whether a build
understands what it's reading. `AG_BOOT_TRAILER_VERSION` is currently `1`,
with no second value defined. `AG_BOOT_EE_VERSION` is `2` (issue #4) —
version 1 ended at byte 128 with `device_serial` followed directly by 58
reserved bytes and the CRC; version 2 inserts `operator_pubkey` after
`device_serial` and moves the reserved span and the CRC down by 64 bytes,
growing the block rather than repurposing bytes the way the handoff record
below does. A region written under version 1 parses as unprovisioned
(`AG_ERR_UNSUPPORTED`) under a version-2-aware build — every field,
`device_serial` included — not partially: see "Reserved EEPROM" above.
`AG_BOOT_HANDOFF_VERSION` is `2` — version 1 held `crc16` at offset 76 and
two reserved bytes at 78; version 2 (issue #18) repurposed the first
reserved byte as `reset_cause` and moved `crc16` to cover it, without
growing the block. An application and a bootloader built from sources on
either side of that change treat each other's handoff exactly like a
missing one: refused, not misread — see "The DICE CDI handoff record"
above. The OTA command IDs (`0x68`–`0x6B`) sit in the same
extend-never-renumber space as the rest of `include/ag/protocol.h`.
