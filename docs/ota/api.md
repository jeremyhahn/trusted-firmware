# OTA — API

> **Status: DRAFT** · Issue: #75, #52, #48 · Scope: trusted-firmware · Last verified: 2026-10-18
> What is on the wire and in `sdk/go` on main. The frames, the other commands
> and the vectors are those of [the wire protocol](../wire-protocol/).

## Wire

An application image built with the stager (`OTA_STAGE=1`, see
[engineering.md](engineering.md)) answers these four commands. An image without
it answers `TFW_ERR_UNKNOWN_CMD` (4). All four are on the Ethernet allow-list
and answer on serial and TCP alike. Multi-byte values are little-endian.

| Id | Command | Request | Reply |
|---|---|---|---|
| `0x68` | `OTA_BEGIN` | `[total_len:4]` | none |
| `0x69` | `OTA_DATA` | `[offset:4][bytes:1..]`, up to `TFW_MAX_PAYLOAD` in all | none |
| `0x6A` | `OTA_END` | none | none |
| `0x6B` | `OTA_STATUS` | none | `[state:1][received:4]` |

`OTA_STATUS`'s `state`:

| Value | State |
|---|---|
| 0 | idle |
| 1 | receiving |
| 2 | complete: the image is checked; no `MARK` yet, `OTA_ACTIVATE` writes it |
| 3 | failed |

Status codes (`include/tfw/status.h`) the stager returns:

| Command | Status | When |
|---|---|---|
| `OTA_BEGIN` | `TFW_ERR_RANGE` (11) | `total_len` is not above the trailer's length, is above the store's `STAGE` area, or is not a whole number of `TFW_OTA_PAGE_MAX` (256) pages |
| `OTA_BEGIN` | `TFW_ERR_NO_STORE` (20) | no part answers READ JEDEC ID (0x9F) on the store's select: the manufacturer byte reads 0x00 or 0xFF |
| `OTA_DATA` | `TFW_ERR_BUSY` (8) | no transfer is receiving |
| `OTA_DATA` | `TFW_ERR_RANGE` (11) | `offset` is not the received count, or the bytes run past `total_len` |
| `OTA_END` | `TFW_ERR_BUSY` (8) | no transfer is receiving |
| `OTA_END` | `TFW_ERR_FRAMING` (2) | fewer than `total_len` bytes arrived |
| `OTA_END` | `TFW_ERR_UNSUPPORTED` (7) | the trailer's magic or version is wrong, or its board id is not this board's |
| `OTA_END` | `TFW_ERR_RANGE` (11) | the trailer's image length is zero or reaches the trailer |
| `OTA_END` | `TFW_ERR_CRYPTO` (14) | the SHA-256 of the image is not the trailer's measurement |
| any | `TFW_ERR_BAD_ARGC` (5) | the payload length is wrong for the command |

`OTA_BEGIN` while a transfer is receiving refuses a bad `total_len` with
`TFW_ERR_RANGE` before it touches the session, so the running upload goes on at
its received count; a valid `total_len` starts a new session with nothing
received.

A store failure during `OTA_BEGIN`, `OTA_DATA` or `OTA_END` returns the store
driver's status. Every failure after `OTA_BEGIN` has been accepted leaves the
state `failed`; a new `OTA_BEGIN` starts over. `OTA_BEGIN` reads the store's
JEDEC ID before it erases anything: a refusal there, or a bus error on that one
transaction, erases nothing and leaves the state `idle`. `OTA_BEGIN` then erases
`MARK` first, so a staged image is never half-replaced.

The stager does not check the signature or the floor. tfw-boot does, at the
next reset ([architecture.md](architecture.md)).

### Activate, confirm and info

ADR decision 8's three control commands (`src/core/commands.def`). They are
serial only: none is on the Ethernet allow-list. `OTA_ACTIVATE` is linked on
every stager, single-slot or A/B (`TFW_FEATURE_OTA_ACTIVATE`); `OTA_INFO` and
`OTA_CONFIRM` on an image that boots a trial (`TFW_FEATURE_OTA_CTL`). An image
without one answers `TFW_ERR_UNKNOWN_CMD` (4).

| Id | Command | Request | Reply |
|---|---|---|---|
| `0x6C` | `OTA_INFO` | `[slot:1]`, 0 running or 1 staged | `[slots:1][active:1][flags:1][floor:4][state:1][image_version:4][measurement:32]`, 44 bytes |
| `0x6D` | `OTA_ACTIVATE` | `[mode:1]`: 0 at the next reset, 1 reset now | none |
| `0x6E` | `OTA_CONFIRM` | none | none |

`OTA_INFO`'s `slots` is the number of slots it reports, always 2 (0 the
running image, 1 the staged one), and `active` the slot that runs, always 0
(`src/core/ota_ctl.c`, ADR decision 8). Its `flags`: `0x01` the last trial fell back, `0x04` a trial is
running. `floor` is the rollback floor, the element's where there is one. For
slot 0, `state` is the platform block's boot state (0 empty, 1 staged, 2 good,
3 trial, 4 confirmed) and `image_version` and `measurement` are zero. For slot 1 it is
4, pending, with the staged image's version and measurement when the store
holds a `MARK`; otherwise the OTA state (0 idle, 1 receiving, 2 verified,
3 failed) and zeros (`include/tfw/boot.h`).

`OTA_ACTIVATE` (`src/core/ota_activate.c`) re-reads and re-verifies the image
`OTA_END` verified and writes `MARK`; mode 1 then resets the device after the
reply, with the application stopped for an update (`tfw_app_before_update`,
then `tfw_app_shutdown`) and no update request armed; mode 0 resets nothing,
and the `RESET` mode 0 that later installs the image stops the application
the same way (`tfw_stage_install_pending`). `OTA_CONFIRM` (`src/core/ota_ctl.c`) writes the confirm request for the
running image, bound to its measurement, and leaves the trial deadline
running (#137), so the trial ends in a reset either way and tfw-boot settles it
at that reset. Until the EEPROM map follow-up #125 the confirm request has no
room for the held confirm signature: `OTA_CONFIRM` takes it and drops it, and
tfw-boot does not yet verify it
([architecture.md](architecture.md), "The confirm keeps the trial deadline").

| Command | Status | When |
|---|---|---|
| `OTA_INFO` | `TFW_ERR_RANGE` (11) | `slot` is above 1 |
| `OTA_INFO` | `TFW_ERR_CRYPTO` (14) | the floor in force cannot be read: on an element image (`CRYPTO=atecc608a`), the element does not wake or its boot counter cannot be read; on a `CRYPTO=auto` image, the handoff's tier is neither the element nor software, or it is the element and the element does not wake or its counter cannot be read. An image with any other `CRYPTO` reports the platform block's floor and never answers it |
| `OTA_INFO` | `TFW_ERR_BAD_STATE` (19) | on a `CRYPTO=auto` image, no running image was handed off by tfw-boot |
| `OTA_ACTIVATE` | `TFW_ERR_RANGE` (11) | `mode` is above 1 |
| `OTA_ACTIVATE`, `OTA_CONFIRM` | `TFW_ERR_AUTH_REQUIRED` (17) | on the element tier, outside an operator auth session; on every tier (#137, slice 2), when no `OTA_AUTH` immediately precedes it |
| `OTA_ACTIVATE` | `TFW_ERR_BAD_STATE` (19) | a trial is running or confirmed and not yet settled; no image is complete; or the staged image no longer hashes to the one `OTA_END` verified |
| `OTA_CONFIRM` | `TFW_ERR_BAD_STATE` (19) | no trial is running, or no running image was handed off by tfw-boot |
| any | `TFW_ERR_BAD_ARGC` (5) | the payload length is wrong for the command |

A store or EEPROM failure returns the driver's status.

### Authorization by the row key (#137, design; code in slice 2)

CA m502 item 2, 2026-10-17, option (a) of #137. On the soft tier anyone on the
wire could activate a verified staged image and confirm a running trial: the
gate passes with no session (`src/core/auth_session_none.c`,
`src/core/auth_session_auto.c`) before `tfw_otactl_cmd_activate`
(`src/core/ota_activate.c:41`) and `tfw_otactl_cmd_confirm`
(`src/core/ota_ctl.c:113`). From slice 2 each command needs an authorization
that tfw-boot checks with the row's verification key, the key that verifies the
staged image. One gate on every tier, with no session, no nonce and no TRNG.

**What is signed.** A COSE_Sign1 (RFC 9052 §4.2) with ES256 (RFC 9053 §2.1),
the protected header `h'A10126'` the SUIT manifest already uses, no external
AAD, and a detached payload (RFC 9052 §2: the payload is `nil` on the wire and
the device rebuilds it from what it already holds). The device sends the
64-byte `r||s` only. The bytes signed are the Sig_structure (RFC 9052 §4.4):

```
["Signature1", h'A10126', h'', bstr .cbor payload]
activate payload: ["tfw-ota-activate", measurement: bstr32, version: uint, mode: uint]
confirm payload:  ["tfw-ota-confirm",  measurement: bstr32, version: uint]
```

- The text string is the domain separation: a confirm signature never
  activates, and an activate signature never confirms.
- `measurement` is the staged image's SHA-256, the trailer measurement
  `OTA_END` verified; for confirm, the running image's, which tfw-boot handed
  off. `version` is that image's trailer `image_version`.
- `mode` is the activate request's byte, so the signer decides whether the
  device resets now.
- **Replay** is stopped by the version against the anti-rollback floor, not by a
  boot counter. tfw-boot already holds both and refuses an image below the
  floor; the floor advances at every confirm, so an authorization for any older
  image is dead once a newer one confirms. The measurement binds it to one
  image, and the key, one per board id, to one board class. A boot counter would
  need a persistent counter and a read round trip before every signature. What
  remains: an authorization replays only for the same image at the same
  version, which its signer already approved once.
- No SUIT construct covers this: SUIT authenticates a manifest
  (draft-ietf-suit-manifest-37 §8.3), and a second envelope with an invoke
  sequence per command would put the envelope parser on the boot path. COSE_Sign1
  over a fixed payload reuses the manifest's algorithm, header and key.

**Who signs.** go-xkms's signing service, which holds the row key; neither this
project nor `sdk/go` holds a private key. go-trusted-platform's `device-ota`
seam builds the Sig_structure, has go-xkms sign it, and sends the signature.

**Where the signed fields come from** (PR #139 review, P2). The signer signs
the identity of the image it built and staged, not what the device reports:

| Field | Source |
|---|---|
| `measurement` (activate) | the SHA-256 of the image the host staged, its trailer `measurement`, the value `OTA_END` verified |
| `version` (activate) | that image's trailer `image_version` |
| `mode` (activate) | the host's choice, the byte it sends in `OTA_ACTIVATE` |
| `measurement`, `version` (confirm) | the same image's trailer values: the image the host activated, now running its trial |

`OTA_INFO` is not the source and cannot be: slot 0 reports zero version and
measurement, and slot 1 reports them only once `MARK` exists, after the
activation being authorized. It is a check after the fact: the host may read it
to see that the device runs, or has staged, the image it signed for. A
signature over any other values fails at tfw-boot, as a wrong key does.

**The wire.** `TFW_MAX_PAYLOAD` is 64 (`include/tfw/config.h:375`), so
`[mode:1][sig:64]` does not fit. The signature arrives in a new command, the
next free id; no request format changes:

| Id | Command | Request | Reply |
|---|---|---|---|
| `0x6F` | `OTA_AUTH` (proposed) | `[sig:64]`, ES256 `r\|\|s` | none |

`OTA_AUTH` holds the signature in RAM for the next command only; any command
consumes or discards it. `OTA_ACTIVATE` with `[mode:1]` and `OTA_CONFIRM` with
no payload keep their ids and formats and take the held signature; without one
they answer `TFW_ERR_AUTH_REQUIRED` (17). An older host's 1-byte `[mode]`
therefore gets a defined refusal with an existing status code, and an older
image answers `OTA_AUTH` with `TFW_ERR_UNKNOWN_CMD` (4). The element tier keeps
its operator session and adds this check: a session alone no longer activates.
The application does not verify (the soft provider has no image verifier,
`src/crypto/soft/provider.c:258`); it stores the signature with what it writes,
and tfw-boot decides ([architecture.md](architecture.md), "Authorization").

### ESP32: the session on `esp_ota` (#48)

An ESP32 image stages through ESP-IDF's `esp_ota`
(`src/hal/esp32/hal_ota.c`, design slices 4 and 5 of
[the ESP32-P4 design](../multi-arch/esp32-p4-design.md) §2.3), not through an
external store. The commands, ids and formats are those above. The ESP32 image
links `src/proto/link_serial.c`, so its TCP wire admits every command it links:
all eight, `OTA_AUTH`, `OTA_ACTIVATE`, `OTA_CONFIRM` and `OTA_INFO` with the
four of the session.

`total_len` and `offset` are u32, so they address the 6 MB slot (`0x600000`).
`OTA_BEGIN` takes the slot ESP-IDF names next
(`esp_ota_get_next_update_partition`) and opens it with
`OTA_WITH_SEQUENTIAL_WRITES`: each sector is erased as `OTA_DATA` reaches it,
so no single command waits on the erase of the whole slot. `total_len` has no
page rule. Where the statuses differ from the table above:

| Command | Status | When |
|---|---|---|
| `OTA_BEGIN` | `TFW_ERR_BAD_STATE` (19) | the running image is a trial (`ESP_OTA_IMG_PENDING_VERIFY`), as tfw-boot refuses a session during one |
| `OTA_BEGIN` | `TFW_ERR_RANGE` (11) | `total_len` is not above the trailer's length, or is above the slot |
| `OTA_BEGIN` | `TFW_ERR_UNSUPPORTED` (7) | the partition table has no slot to update |
| `OTA_DATA` | `TFW_ERR_CRYPTO` (14) | the first byte is not the ESP image magic (`ESP_ERR_OTA_VALIDATE_FAILED`) |
| `OTA_DATA` | `TFW_ERR_RANGE` (11) | as above, or the write runs past the slot |
| `OTA_END` | `TFW_ERR_CRYPTO` (14) | `esp_ota_end`'s image check fails: the image's structure, its SHA-256 or, under secure boot, its signature |

`OTA_END` still means "every byte arrived". ESP-IDF's check is a stronger
refusal on top of that, with the existing verify-failure status; it does not
change what an accepted `OTA_END` means. Any other ESP-IDF error answers
`TFW_ERR_INTERNAL` (18). Every failure after `OTA_BEGIN` has been accepted
leaves the state `failed`, and a new `OTA_BEGIN` starts over, as above.

**Activate.** `OTA_ACTIVATE` takes the held `OTA_AUTH` signature, re-checks
the completed slot as layer 0 will (trailer, board id, the floor, the
measurement against the slot's SHA-256), writes the signature and `mode` to the
`tfw_auth` partition and sets the slot to boot with
`esp_ota_set_boot_partition`; mode 1 then resets after the reply. The
session's state is in RAM, so `OTA_ACTIVATE` follows `OTA_END` with no reset
between. The application does not verify the signature. At the next reset ESP-IDF's
bootloader marks the new slot `ESP_OTA_IMG_PENDING_VERIFY`, and layer 0
(`boot/hal/esp32/boot_l0.c`) verifies the activate Sig_structure over the
trailer's measurement and version with the image key before it runs the trial.
A refused trial is not run: layer 0 boots the slot that ran before, and the next
reset finds the entry still pending and ESP-IDF marks it `ESP_OTA_IMG_ABORTED`.

**Confirm.** `OTA_CONFIRM` on a trial calls
`esp_ota_mark_app_valid_cancel_rollback`: the trial ends there, with no reset.
It takes the held signature and does not verify it; nothing on the ESP32
verifies a confirm signature yet. A trial nobody confirms resets at
`TFW_OTA_TRIAL_TIMEOUT_S` (60 s on both ESP32 boards, `src/core/ota_trial.c`),
and ESP-IDF rolls back to the slot that ran before.

**Anti-rollback.** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is on for every ESP32
board. ESP-IDF's anti-rollback is on where the board profile names the target
in `ESP_ANTI_ROLLBACK_TARGETS`: today the esp32c3 under QEMU only, whose eFuses
are a file. There the build stamps `ESP_IMAGE_VERSION` into both the trailer's
`image_version` and the app's `secure_version`, `OTA_ACTIVATE` and layer 0
refuse an image where they differ (`TFW_ERR_PROTECTED_REGION`, 16), and
ESP-IDF raises the eFuse floor to the image's `secure_version` when the image is
confirmed. Which releases advance the floor on a real board is the owner's
decision; no board's eFuses are burned until it is made.

| Command | Status | When |
|---|---|---|
| `OTA_ACTIVATE` | `TFW_ERR_RANGE` (11) | `mode` is above 1; or the slot's image is not one ESP-IDF can describe, has no appended SHA-256, or leaves no room for the trailer |
| `OTA_ACTIVATE`, `OTA_CONFIRM` | `TFW_ERR_AUTH_REQUIRED` (17) | no `OTA_AUTH` immediately precedes it |
| `OTA_ACTIVATE` | `TFW_ERR_BAD_STATE` (19) | the session is not complete, the running image is a trial, or ESP-IDF refuses the boot partition |
| `OTA_ACTIVATE` | `TFW_ERR_UNSUPPORTED` (7) | the trailer's magic or version is wrong, or its board id is not this board's |
| `OTA_ACTIVATE` | `TFW_ERR_PROTECTED_REGION` (16) | the trailer's version is below the eFuse floor; under anti-rollback, also when it is not the app's `secure_version` |
| `OTA_ACTIVATE` | `TFW_ERR_CRYPTO` (14) | the trailer's image length or measurement is not the slot's |
| `OTA_CONFIRM` | `TFW_ERR_BAD_STATE` (19) | the running image is not a trial |
| `OTA_CONFIRM` | `TFW_ERR_INTERNAL` (18) | ESP-IDF cannot mark the image valid |

A failed write to `tfw_auth` returns its status, and a failed read
`TFW_ERR_INTERNAL` (18).

`OTA_INFO` on the ESP32:

- `slots` is 2 and `active` is 0. `floor` is the eFuse secure version. The
  application never writes it; under anti-rollback ESP-IDF raises it at a
  confirm.
- `flags`: `0x04` when the running slot is `ESP_OTA_IMG_PENDING_VERIFY`, and
  `0x01` when the other slot is `ESP_OTA_IMG_ABORTED` or `ESP_OTA_IMG_INVALID`,
  or still `ESP_OTA_IMG_PENDING_VERIFY` while this one runs: layer 0 refused
  that trial at its one boot.
- Slot 0 is the running partition. Its `state` is 3 (trial) or 2 (good), and
  its `image_version` and `measurement` are real: the app description's
  `secure_version` (0 where anti-rollback is off, since the build stamps it
  only there) and the image's SHA-256.
- Slot 1 is the session's slot. `state` is the OTA state. Once that state is
  complete, `image_version` and `measurement` are the slot's; until then both
  are zero.

## `sdk/go`

`sdk/go/ota.go`, on `*tfw.Device` (and on `*tfw.TCPDevice`, which embeds it):

| Name | Does |
|---|---|
| `OTABegin(ctx, total uint32) error` | `OTA_BEGIN` |
| `OTAData(ctx, offset uint32, chunk []byte) error` | one `OTA_DATA` |
| `OTAEnd(ctx) error` | `OTA_END` |
| `OTAStatus(ctx) (OTAState, uint32, error)` | `OTA_STATUS`: the state and the received count |
| `StageImage(ctx, image []byte) error` | `OTA_BEGIN`, then `OTA_DATA` in chunks of `OTAChunk` (60) bytes, then `OTA_END` |

`OTAState` has `OTAIdle`, `OTAReceiving`, `OTAComplete` and `OTAFailed`, the
values above. `StageImage` sends the image as given: it neither pads it nor
checks its trailer. A device status comes back as the SDK's status error.

The same calls drive tfw-boot's own serial session, which takes the same four
commands.
