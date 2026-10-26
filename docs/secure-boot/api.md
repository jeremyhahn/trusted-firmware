# tfw-boot — API

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

One transport: the framed UART protocol (`src/proto/frame.c`), polled, no
interrupts. `tfw-boot` has no REST, gRPC, QUIC, MCP or Unix-socket surface —
this repo owns the AVR wire protocol and nothing above it (see the top-level
`CLAUDE.md`). Requests and responses use the same frame codec the application
uses, which is the whole reason OTA frames and application frames cannot
drift apart. Definitions are `include/tfw/boot.h`, `include/tfw/protocol.h` and
`include/tfw/status.h`.

## Commands

Handled by `tfw_boot_session_handle` (`boot/boot_session.c`), reachable only
when the bootloader is in its OTA recovery path
(`boot/recovery/recovery_ota.c`, linked when `BOOT_OTA=1`). Every other
command — the application's full set — returns `TFW_ERR_UNKNOWN_CMD`, which is
how a host distinguishes "talking to the bootloader" from "talking to a dead
device."

| Command | ID | Request payload | Response payload | Notes |
|---|---|---|---|---|
| `TFW_CMD_PING` | `0x01` | 0–N bytes, echoed | same bytes | the only command answered identically by both images; used to tell "in the bootloader, waiting" from "dead" |
| `TFW_CMD_RESET` | `0x02` | `[mode:1]` | none | `TFW_BOOT_SESSION_RESET`; the loop calls `tfw_boot_reset()` after the response frame is sent |
| `TFW_CMD_OTA_BEGIN` | `0x68` | `[total_len:4]` | none | declares the whole image length, trailer included; refuses a length that is not a whole page count or does not reach `TFW_BOOT_TRAILER_LEN`. No version argument — the version lives inside the trailer, under the signature |
| `TFW_CMD_OTA_DATA` | `0x69` | `[offset:4][bytes:1..]` | none | `offset` must match the bytes the session has received so far exactly; a chunk stream with no offset would let one dropped frame silently shift everything after it |
| `TFW_CMD_OTA_END` | `0x6A` | none | none | refuses unless exactly `declared_len` bytes arrived; flushes a partial final page padded with `0xFF`. Success here means every byte arrived — **nothing has been verified**. The session action is `TFW_BOOT_SESSION_COMMIT`, which makes the recovery loop run `tfw_boot_evaluate` next |
| `TFW_CMD_OTA_STATUS` | `0x6B` | none | `[state:1][received:4]` | `state` is `tfw_ota_state_t`: `TFW_OTA_IDLE`(0) / `RECEIVING`(1) / `COMPLETE`(2) / `FAILED`(3) |

Status byte in every response frame is `tfw_status_t`. The ones this surface
can return: `TFW_OK`(0), `TFW_ERR_BAD_ARGC`(5, wrong payload length for the
command), `TFW_ERR_RANGE`(11, a length or offset outside what's possible),
`TFW_ERR_PROTECTED_REGION`(16, an OTA write reaching the boot section),
`TFW_ERR_UNKNOWN_CMD`(4, any application command). `OTA_END`'s
`TFW_BOOT_SESSION_COMMIT` triggers a **second**, unsolicited `EVENT` frame
after the response — the result of `tfw_boot_evaluate` on the just-received
image — carrying `TFW_ERR_PROTECTED_REGION` (downgrade), `TFW_ERR_CRYPTO`
(measurement or signature failed), `TFW_ERR_NOT_PROVISIONED` (no usable key),
or `TFW_OK` before the device jumps.

### Entering the session from a healthy device

The loop above is also entered on purpose: the application's `RESET` with mode 1
(see [../wire-protocol/api.md](../wire-protocol/api.md#reset-into-the-bootloader))
leaves the request described in `include/tfw/boot.h` and tfw-boot opens the same
loop through `tfw_boot_session_open(plat, &limits)` instead of jumping.
Differences from recovery:

- The entry `EVENT` carries `TFW_OK` and no payload ("you asked for me"), not a
  refusal reason.
- The loop is **bounded**, through `tfw_boot_session_open(plat, &limits)`, and
  whichever bound is reached first ends it. Ending it means tfw-boot continues
  into `tfw_boot_evaluate` and the jump. There is no timer and no delay in
  tfw-boot, so the units are loop iterations and bytes; `include/tfw/config.h` and
  `include/tfw/boot.h` convert durations from `F_CPU` and the baud rate.

  | Bound | Counts | Restarts on an accepted `OTA_BEGIN`/`DATA`/`END` | Stops |
  |---|---|---|---|
  | `idle_polls` | consecutive empty receive polls | yes | a quiet line |
  | `idle_bytes` | bytes received since the last accepted frame | yes | junk, partial frames, PINGs at line rate |
  | `max_polls` | every loop iteration, empty or not | **no** | a slow drip of valid frames |
  | `max_bytes` | every byte received | **no** | a fast flood of valid frames |

  An accepted frame is well-formed, not authenticated, so the first two cannot
  be trusted to bound anything on their own; the last two are the ceiling that
  nothing resets. Defaults: `TFW_BOOT_SESSION_IDLE_SECONDS` 10 s;
  `TFW_BOOT_SESSION_MAX_SECONDS` 60 s (six idle windows);
  the poll limits are those seconds times the boot HAL's `TFW_BOOT_TICK_HZ` --
  on AVR `F_CPU / TFW_BOOT_POLL_CYCLES`, 77 cycles per iteration
  (`boot/hal/avr/boot_tick.h`), measured by the simavr suite, which fails if
  the real duration drifts; `TFW_BOOT_SESSION_MAX_BYTES`
  = application size + 10 bytes of framing per 32 data bytes + 16 + 8 KB slack
  (about 83 KB on the ATmega2560, 46 KB on the 28 KB parts).
  **Margin for a full legitimate update**, ATmega2560, 56 KB at 115200 baud,
  stop-and-wait in 60-byte chunks: about 940 frames, roughly 6 s of request wire
  time, 1 s of responses and 2 s of page writes, i.e. about 9 s with an instant
  host; 60 s allows 64 ms of host latency per frame. In bytes the transfer uses
  about 66 KB of the 83 KB ceiling (and fits it at 32-byte chunks with the whole
  slack spare). The 28 KB parts have half the image and the same time ceiling.
- **One attempt.** The first `OTA_END` that does not end in a verified image --
  refused as short or out of order, or failed by `tfw_boot_evaluate` -- ends the
  requested session, so a bogus transfer cannot be followed by another.
- **What falling through costs.** `OTA_BEGIN` writes nothing; `OTA_DATA` erases
  and rewrites application pages as each page fills, in place (single slot). A
  session that ends before the first page was written leaves the old
  application intact and it boots. One that ends after has already destroyed the
  application it was replacing, so the ordinary measure/verify fails and the
  device lands in recovery, which is unbounded and waits for a good image. That
  is correct and expected, and it is the residual risk of a single-slot layout.
  `BOOT_OTA=0` has no session at all and ignores the request.
- Recovery's loop never times out; a session's does.

## Update-session request

Eight bytes at the start of the CDI handoff region: `TFW_BOOT_REQUEST_MAGIC`
(`0x21445055`) then its complement, little-endian. The region is the one SRAM
span both images already agree on (placed by the same formula at link time,
never zeroed by the C runtime, and free once the application has consumed the
handoff). Honoured only if `tfw_boot_request_valid`: both words match **and**
tfw-boot's reset cause (`tfw_boot_reset_cause()`) is `TFW_RESET_WATCHDOG` — on
AVR the HAL reports that only for a watchdog reset with no power-on, external
or brown-out flag beside it. `tfw_boot_request_take` clears it unconditionally before
anything else in `main()`, so an abandoned or timed-out session never repeats.
Shared encoder/validator: `boot/boot_request.{c,h}`; the application's side is
`src/core/update_request.c` (`_none.c` on boards with no boot section).

`tfw-boot` also emits one unsolicited `EVENT` frame on entry to recovery,
carrying the `tfw_status_t` reason it refused to boot — the same set
`tfw_boot_evaluate` can return (see [engineering.md](engineering.md)'s error
table).

## Structures

All three are defined once in `include/tfw/boot.h` because each has a second
party that must agree byte for byte, and every field is read by shift and
mask — never a struct cast — so no compiler's padding rules become part of
the wire contract.

### The image trailer

The last `TFW_BOOT_TRAILER_LEN` (128) bytes of the application region, at
`TFW_BOOT_TRAILER_ADDR = TFW_BOOT_APP_END - 128`:

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | `magic` | `TFW_BOOT_TRAILER_MAGIC` = `0x4D494654` |
| 4 | 2 | `trailer_version` | `TFW_BOOT_TRAILER_VERSION` = 1, exact match required |
| 6 | 1 | `board_id` | `TFW_BOOT_TR_BOARD_ID`; the board-id registry's `TFW_BOARD_ID`. `tfw-boot` refuses an image whose `board_id` is 0 or not its own, before the anti-rollback floor |
| 7 | 1 | `image_flags` | `TFW_BOOT_TR_IMAGE_FLAGS`; bit 0 `TFW_BOOT_IMAGE_FLAG_DEV` marks a development image; bits 1-7 are reserved, and an image with any of them set is refused |
| 8 | 4 | `image_version` | the anti-rollback security version number; 0 is refused |
| 12 | 4 | `image_len` | bytes measured, from address 0; bounded by `TFW_BOOT_MAX_IMAGE_LEN` |
| 16 | 32 | `measurement` | SHA-256 over `[0, image_len)` — this is `FWHash` |
| — | — | *signed span ends here* | bytes `[0, 48)`, `TFW_BOOT_TR_SIGNED_LEN` |
| 48 | 64 | `signature` | ECDSA over P-256 and SHA-256 (ES256), `r`‖`s`, over the signed span — every row verifies it, the soft tier with the vendored micro-ecc (`verify_p256_soft.c`), the element tier in the ATECC608A |
| 112 | 16 | reserved | `TFW_BOOT_TR_RESERVED`; zero, and a trailer with any of them set is refused `TFW_ERR_UNSUPPORTED` before the signature is checked: they are outside the signed span |

Parsed by `tfw_boot_trailer_parse`, serialised by `tfw_boot_trailer_serialise`
(`boot/boot_image.c`); `tfw_boot_binding_permit` beside them decides the
`board_id` and `image_flags` check. The build-time signing tool and the
OTA-sending host tool are the two other parties; the signer takes `board_id`
from the board profile and `image_flags` from the build (`IMAGE_FLAGS`).

### Reserved EEPROM

`TFW_BOOT_EE_BASE` (`0x000`) through `0x13F`, `TFW_BOOT_EE_LEN` (`0x140`, 320) bytes,
refused for application writes by `src/caps/storage.c`:

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | `magic` | `TFW_BOOT_EE_MAGIC` = `0x52504654` |
| 4 | 1 | `layout_version` | `TFW_BOOT_EE_VERSION` = 2 (grew from 1 when `operator_pubkey` below was added — issue #4; an older-layout region parses as unprovisioned rather than partially) |
| 5 | 1 | `flags` | |
| 6 | 2 | reserved | |
| 8 | 32 | `pubkey_hash` | SHA-256 of the 32-byte Ed25519 public key; ignored on the element tier |
| 40 | 4 | `rollback_floor` | software tier only; the element tier uses its own monotonic counter |
| 44 | 4 | `boot_count` | |
| 48 | 1 | `boot_state` | `TFW_BOOT_STATE_EMPTY`(0) / `STAGED`(1) / `GOOD`(2) / `TRIAL`(3) / `CONFIRMED`(4) — the one trial, [../ota/architecture.md](../ota/architecture.md#what-runs-today-77); extended, never renumbered |
| 49 | 1 | `ota_state` | `TFW_BOOT_OTA_IDLE`(0) / `RECEIVING`(1) / `VERIFIED`(2) / `FAILED`(3) / `PENDING`(4) |
| 50 | 2 | `boot_fail_count` | |
| 52 | 16 | `device_serial` | baked into the `.eep` image by `tfwprov --bake-eeprom` before the part is ever flashed — there is no wire command that writes it |
| 68 | 64 | `operator_pubkey` | the `AUTH_CHALLENGE`/`AUTH_RESPONSE` session's trust anchor (issue #4), P-256 as X‖Y; element tier only, baked in the same way as `device_serial`; see [../security/threat-model.md](../security/threat-model.md) |
| 132 | 58 | reserved | |
| 190 | 2 | `crc16` | over bytes `[0, 190)` — corruption detection, **not** integrity against an attacker; anyone who can write the region can recompute it |

Parsed by `tfw_boot_platform_parse`, serialised by `tfw_boot_platform_serialise`
(`boot/boot_platform.c`); accessed through `tfw_boot_platform_load` /
`tfw_boot_platform_store` (`boot/boot_store.c`). The second party is `tfwprov`
(`sdk/go/cmd/tfwprov`) — see
[../device-provisioning/user.md](../device-provisioning/user.md).

### The DICE CDI handoff record

`TFW_BOOT_HANDOFF_LEN` (112) bytes of SRAM in its own `.handoff` section --
uninitialised-but-not-zeroed storage, the same idea `.noinit` names, but its
own section rather than that one: `src/hal/avr/hal_early.c`'s
`tfw_hal_early_flags` lives in ordinary `.noinit` for the identical reason
(surviving past `.init3`, before `.bss` is cleared), and pinning the handoff
block's address onto the generic `.noinit` output section would carry that
variable along with it — briefly true of this codebase and the reason this
note is here. At the top of RAM, written once by `tfw_boot_handoff_write` and
read once by the application (`src/core/handoff.c`'s `tfw_boot_handoff_init`,
at startup, before `tfw_crypto_init`):

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | `magic` | `TFW_BOOT_HANDOFF_MAGIC` = `0x49444341` |
| 4 | 1 | `version` | `TFW_BOOT_HANDOFF_VERSION` = 4 |
| 5 | 1 | `tier` | the `tfw_crypto_tier_t` `tfw_boot_cdi_tier()` observed |
| 6 | 2 | `flags` | |
| 8 | 32 | `cdi` | `HMAC(UDS, measurement)` |
| 40 | 32 | `measurement` | so the application reports the `FWHash` actually verified, not a recomputation |
| 72 | 4 | `image_version` | |
| 76 | 1 | `reset_cause` | `tfw_reset_cause_t`, decoded by the boot HAL from the latch it took before tfw-boot's own `.init3` cleared the register — issue #18, abstracted by issue #10 |
| 77 | 1 | reserved | |
| 78 | 2 | `crc16` | over every byte but its own two: `[0, 78)` and `[80, 112)` |
| 80 | 32 | `device_seed` | `HMAC(UDS, TFW_DEVICEID_LABEL)`, the P-256 DeviceID's seed; all zero where this tfw-boot holds no UDS |

Version 2 repurposed bytes rather than growing the block: version 1 held
`crc16` at offset 76 and two reserved bytes at 78; version 2 moved `crc16` to
the end of what it now covers, widening coverage to include `reset_cause`.
`TFW_BOOT_HANDOFF_LEN` is unchanged. Version 3 (issue #10) keeps the layout and
changes what byte 76 means: version 2 carried the raw `MCUSR` there, version 3
the abstract `tfw_reset_cause_t`, so neither side decodes a register and a
version 2 block is refused rather than reinterpreted. Version 4 (issue #173)
grows the block at its end: `device_seed` goes after `crc16`, `crc16` stays at
78 and now covers the seed as well, and `TFW_BOOT_HANDOFF_LEN` goes from 80 to
112 bytes, with it the region every image places at the top of SRAM.

Built by `tfw_boot_handoff_build`, parsed by `tfw_boot_handoff_parse`
(`boot/boot_handoff.c`) — pure functions over the byte layout, testable
without an MCU on either side. `tfw_boot_handoff_parse` returns
`TFW_ERR_NOT_PROVISIONED` on a missing magic, `TFW_ERR_UNSUPPORTED` on a version
this build doesn't know, and `TFW_ERR_CRC` on a block a stack overwrote — the
one failure worth naming on a part with no MPU. An unknown or older version
degrades exactly like a missing handoff: `DeviceID`/`FWHash` stay zero and
`tfw_hal_reset_cause()` falls back to the application's own `.init3`
observation — never a block misread as something it is not.

**This is data, never a call.** The application asking the bootloader for its
CDI through a trampoline is exactly what the tier forbids — see
[architecture.md](architecture.md)'s trust boundaries.

## Versioning

Every layout above is additive-only: offsets never move, fields never
renumber, and a `*_version`/`layout_version` byte gates whether a build
understands what it's reading. `TFW_BOOT_TRAILER_VERSION` is currently `1`,
with no second value defined. `TFW_BOOT_EE_VERSION` is `2` (issue #4) —
version 1 ended at byte 128 with `device_serial` followed directly by 58
reserved bytes and the CRC; version 2 inserts `operator_pubkey` after
`device_serial` and moves the reserved span and the CRC down by 64 bytes,
growing the block rather than repurposing bytes the way the handoff record
below does. A region written under version 1 parses as unprovisioned
(`TFW_ERR_UNSUPPORTED`) under a version-2-aware build — every field,
`device_serial` included — not partially: see "Reserved EEPROM" above.
`TFW_BOOT_HANDOFF_VERSION` is `4` — version 3 (issue #10) changed byte 76 from
the raw `MCUSR` to the abstract reset cause without moving it, and version 4
(issue #173) appended `device_seed` after the `crc16` at offset 78, widening
the CRC to cover it and growing the block from 80 to 112 bytes. An
application and a bootloader built from sources on either side of a version
change treat each other's handoff exactly like a missing one: refused, not
misread — see "The DICE CDI handoff record" above. The OTA command IDs
(`0x68`–`0x6B`) sit in the same extend-never-renumber space as the rest of
`include/tfw/protocol.h`.
