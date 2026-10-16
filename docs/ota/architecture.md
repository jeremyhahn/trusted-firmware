# OTA — architecture

> **Status: ACCEPTED where the ADR's decisions are ACCEPTED; PROPOSED where
> they are** · Issue: #52 · Scope: trusted-firmware
> (image format, device side, `sdk/go` upload helper); go-trusted-platform,
> go-quicbaas, go-xkms and qrdb (seams only) · Last verified: 2026-10-15
>
> Decisions, their reasons and the measured flash are in
> [adr-ota-image-format-and-trust.md](adr-ota-image-format-and-trust.md). No
> code exists for anything here that is not already shipped by
> [`secure-boot/`](../secure-boot/architecture.md), the trailer's board-id
> check (#76), the trial state machine (#77) or the manifest gate (#78). The SPEC row `ota` stays
> `future`.

OTA replaces the whole image, firmware and application together, with a signed
one, and never lowers its security version. Every board with a boot section
takes its image over the network (W5100 or W5500) and over serial. The new
image boots once on trial. The control plane confirms it after attesting the
device; if it does not, the device rolls back by itself, and recovery is another
OTA round.

This file holds the shape: who owns what, how an image travels, and what the
device does with it.

## Ownership

| Piece | Owner | This project's part |
|---|---|---|
| Image format: trailer v1 with the board id, its signed span, the measurement | trusted-firmware | owns it |
| The device's manifest profile (a SUIT subset, ES256) and the build's to-be-signed manifest | trusted-firmware | owns the profile and the encoder; signs nothing |
| Verifying an image before it runs, the anti-rollback floor, slots, the trial | trusted-firmware (`tfw-boot`), or MCUboot, ESP-IDF or the vendor boot ROM where a port composes one | owns it, or composes theirs |
| The upload helper: driving `OTA_*` on a device connection | trusted-firmware (`sdk/go`) | owns it |
| Which device gets which image, when, in which wave; pausing a rollout; the confirm after attestation | go-trusted-platform (fleet policy, rollout, control plane) | none: states the seam |
| The device-facing endpoint the device connects to | go-trusted-platform | none: states the seam |
| Image bytes, their signed SUIT envelope and their signed CoRIM | go-quicbaas (reference-values store) | none: states the seam |
| The image key and the manifest signer, per board id; CoRIM signing; appraisal of the post-update `FWHash` | go-xkms, reached through go-quicbaas | none: states the need |
| Placing the verification key in the element's slot | go-xkms's provisioning seam row; `tfwprov` writes the slot on the bench | the bench write |
| The permissions `ota.activate` and `ota.confirm` | qrdb (RBAC vocabulary), checked by go-trusted-platform | none: states the seam |

This project keeps no registry, store, update server or private key. A gap in
any of the other repos is reported against that repo, never worked around
here.

## Data flow

```
build (reproducible) ──► image + trailer v1 TBS (48 B) + SUIT manifest TBS
                              │  go-quicbaas ──► go-xkms: the board id's P-256 key,
                              │  hardware-backed; raw r‖s and COSE_Sign1 come back
                              ▼
                     signed image + signed SUIT envelope + signed CoRIM
                              │      (one measurement in all three)
                              ▼
                 go-quicbaas reference-values store
                              │
   go-trusted-platform ───────┤ picks an image for (board id, application, profile)
   rollout                    │ checks the digest against the CoRIM and the envelope
                              ▼
device ──(opens TCP or serial)──► go-trusted-platform device-facing endpoint
                              │ auth session (AUTH_CHALLENGE, AUTH_RESPONSE)
                              │ sdk/go upload helper: OTA_BEGIN, OTA_DATA…, OTA_END
                              │ OTA_ACTIVATE  (principal holds ota.activate)
                              ▼
device application: stage, gate (trailer, board id, floor, SHA-256, signature, manifest)
device boot:        copy or swap, evaluate (authoritative), one trial
                              │
device ──(reconnects)──► OTA_INFO, ATTEST ──► go-trusted-platform
                              │ appraises the new FWHash against the CoRIM
                              │ OTA_CONFIRM  (principal holds ota.confirm)
                              ▼
device: CONFIRMED ──next reset──► floor advances
```

- The device opens the connection; it never listens. That is the Ethernet
  transport's rule, and OTA does not change it.
- The host side of every `OTA_*` exchange is `sdk/go`, imported by
  go-trusted-platform. On a bench it is `tfwctl`, on a serial port or TCP.
- The device trusts the signature, the board id and the floor. The CoRIM is
  checked by the control plane before it sends an image, and by the verifier
  after the device reports the new `FWHash`. A device running an image outside
  the store fails appraisal at its next attestation and is never confirmed.

## Arch classes

| Class | Parts | Layout | Receives over | Who verifies | Floor |
|---|---|---|---|---|---|
| AVR, no boot section | ATtiny85 | none: no secure boot, no OTA (proposed exemption) | — | nobody | none |
| AVR, 4 KB boot section | ATmega328P, ATmega32U4 | one internal slot; the staged image and a backup of the running one in an external SPI store | network and serial, in the application | the application gates; `tfw-boot` verifies, copies and evaluates | the element's monotonic counter |
| AVR, 8 KB boot section | ATmega2560 | A/B by swap-move; network images staged in an external SPI store | network and serial in the application; serial in `tfw-boot` too | as above, plus the manifest in the application | as above |
| Cortex-M33 under TF-M ([trust-model ADR](../cortex-m/adr-trust-model-vs-tf-m.md) (b1)) | STM32H5, nRF54L15, MCXN947, RP2350 | MCUboot's primary and secondary slots, written through PSA Firmware Update | the non-secure application | MCUboot (BL2) | MCUboot's per-image security counter |
| Cortex-M, `tfw-boot` port | an M33 under option (a); an M7 without an SoC root of trust | A/B: a hardware bank swap where the part has one, swap-move otherwise | the application | `tfw-boot` port | a counter in OTP or secure flash, per port ADR |
| Composed vendor boot | ESP32-P4 (ESP-IDF) | ESP-IDF's `ota_0`/`ota_1` and `otadata` | the application | ESP-IDF secure boot v2, plus our trailer v1 (`board_id` `0x80`) | the eFuse secure version |
| Platform image | i.MX 95 M7 | pending #15 | pending #15 | AHAB and the ELE | pending #15: the AHAB fuse version is per container set |

**AVR stages outside the MCU.** On classic AVR only boot-section code can
execute `SPM`, and an entry point the application could call would void the
BLB1 read barrier that keeps the UDS out of its reach. The W5x00 session does
not fit either boot section: it is about 2 KB, and the 2560 is over by 660 to
1 230 bytes, the 328P by more than 4 KB. So the application receives the image,
over Ethernet or serial, and streams it into an SPI store on the shield bus.
`tfw-boot` takes it from the store at the next reset. The boot never parses
network input.

**What each AVR image carries.**

| Board | Boot | Application | Transports in the shipped image |
|---|---|---|---|
| ATmega2560 | serial session, swap-move, store read, trial: 7 924 of 8 192 | `ETH=1`, stager, manifest reader: about 24 445 of 65 536 | both |
| ATmega328P | verify-and-copy, trial: 3 924 of 4 096 | `ETH=1`, stager: about 19 267, and about 22 153 with the manifest reader | both, under the 28 672 region budget (proposed); none under the 16 384 policy line |
| ATmega32U4 | as the 328P, unmeasured | no Ethernet controller wired yet | follow-up |

## Device state machine

### Slot states

Every class reports the same states through `OTA_INFO`. On AVR they are encoded
in the platform block's `boot_state` and `ota_state`, extended and never
renumbered (ADR, open question 10). The bootloader owns the transitions marked
`boot`; the application owns the rest.

| State | Meaning | Left by |
|---|---|---|
| `EMPTY` | no image staged | `OTA_BEGIN` (app) → `RECEIVING` |
| `RECEIVING` | bytes arriving into the store or the inactive slot | `OTA_END` with every byte → `STAGED`; a new `OTA_BEGIN` restarts |
| `STAGED` | every byte arrived; nothing verified | the application's gate → `VERIFIED` or `FAILED` |
| `VERIFIED` | trailer, board id, floor, measurement, signature and, where verified, the manifest passed the gate | `OTA_ACTIVATE` → `PENDING`; refused with `TFW_ERR_BAD_STATE` if the measurement is the last failed one |
| `PENDING` | to be tried at the next reset | boot: copy or swap, evaluate → `TRIAL`, or roll back → `FAILED` |
| `TRIAL` | running once, not confirmed; the trial deadline and the watchdog are armed | `OTA_CONFIRM` → `CONFIRMED`; **any** reset → boot rolls back → `FAILED` |
| `CONFIRMED` | confirmed by the control plane; the floor not yet advanced | boot, next reset: floor := version → `GOOD`; `OTA_BEGIN` is refused until then |
| `GOOD` | the image the device boots | replaced when another image confirms |
| `FAILED` | refused, or the trial ended unconfirmed; its measurement is kept | overwritten by the next `OTA_BEGIN` |

### ATmega2560: A/B by swap-move

```
application: EMPTY/FAILED ─OTA_BEGIN─► RECEIVING ─OTA_END─► STAGED ─gate─► VERIFIED
               (network: into the SPI store; serial: into the store, or the
                boot session writes the secondary slot directly)
             VERIFIED ─OTA_ACTIVATE─► PENDING ─reset─►
boot:        PENDING: verify the store copy, copy it into the secondary slot,
             swap-move (progress in EEPROM), evaluate the primary
               ─ok─► TRIAL, arm the watchdog ─jump─►
               ─refused─► swap back, FAILED, evaluate and boot the previous image
application: init OK, tfw_app_after_update OK ─► connect, attest, wait
             ─OTA_CONFIRM─► CONFIRMED          (deadline expired ─► reset)
boot:        TRIAL at reset ─► swap back, FAILED, flags: fell back
             CONFIRMED at reset ─► floor := version, GOOD, then anything PENDING
```

### ATmega328P and ATmega32U4: store-staged, one internal slot

```
application: OTA_BEGIN ─► back the running image up to the store's PREV
             OTA_DATA… ─► the store's STAGE  ─OTA_END─► STAGED ─gate─► VERIFIED
             VERIFIED ─OTA_ACTIVATE─► PENDING ─reset─►
boot:        PENDING: copy STAGE into the application region (idempotent),
             TRIAL, evaluate the internal copy (authoritative)
               ─ok─► arm the watchdog ─jump─►
               ─refused─► copy PREV back, FAILED, flags: refused
application: as the 2560
boot:        TRIAL at reset ─► copy PREV back, FAILED, flags: fell back,
             evaluate PREV as every boot ─ok─► run it ─refused─► halt (programmer)
             CONFIRMED at reset ─► floor := version, GOOD
```

- The bootloader evaluates the image it is about to boot, on every boot, in the
  same order: trailer, board id, floor, measurement, signature. That includes
  the restored or swapped-back fallback. The application's gate saves a reboot
  into a refusal; it is not a security boundary.
- **One trial.** The trial state is written before the jump, so any reset in
  it, whether the deadline, the watchdog, a crash or a power cut, rolls back.
  There is no attempt counter. The same image is not activated again.
- **The timeout** is per board, `TFW_OTA_TRIAL_TIMEOUT_S` in
  `boards/*/board_profile.h` (name and values proposed). The application
  counts it; `tfw-boot` arms the watchdog, which the application SDK pets during
  the trial.
- **The floor advances after confirm**, at the next reset and before any pending
  image is handled. Advancing it on the trial boot would put the fallback below
  the floor, and it would be refused.
- **After a rollback**, the restored image is the full previous application,
  network and stager included, so the next round starts exactly as the first.
  It reports `FAILED`, the fallback flags and the failed measurement in
  `OTA_INFO`, and its old `FWHash` at attestation.

### What runs today (#77)

The transitions are pure functions in `boot/boot_trial.{c,h}`, over the two
state bytes of the platform block; a transition in the wrong state answers
`TFW_ERR_BAD_STATE` (19) and changes nothing. Host tests drive every
transition and every refusal (`test/unit/boot/test_boot_trial.c`). The
encoding, from `include/tfw/boot.h`:

| Byte | Values |
|---|---|
| `boot_state` | `EMPTY` 0, `STAGED` 1, `GOOD` 2, `TRIAL` 3, `CONFIRMED` 4 |
| `ota_state` | `IDLE` 0, `RECEIVING` 1, `VERIFIED` 2, `FAILED` 3, `PENDING` 4 |

| Function | Transition | Refused (`TFW_ERR_BAD_STATE`) |
|---|---|---|
| `tfw_boot_trial_at_reset` | picks, in order: `TRIAL` → fail, `CONFIRMED` → settle, `PENDING` → try, else boot | never |
| `tfw_boot_trial_arm` | `PENDING` → `TRIAL`, before the jump | nothing pending; a trial running; a confirm not yet settled |
| `tfw_boot_trial_fail` | `TRIAL` or `PENDING` → `TRIAL`, `FAILED`, so every later reset fails again; a pending image refused at evaluation never boots, even once a transient failure clears | no trial and nothing pending |
| `tfw_boot_trial_confirm` | `TRIAL` → `CONFIRMED` | outside a trial; a trial already failed |
| `tfw_boot_trial_settle` | `CONFIRMED` → `GOOD`, floor := version | no confirm |
| `tfw_boot_trial_activate` | `VERIFIED` → `PENDING` | no verified image; a trial running; the measurement that last failed |
| `tfw_boot_trial_begin_permit` | — | `OTA_BEGIN` while an image is pending; while a confirm is not yet settled |

The only path that exists yet is the in-place boot session (`recovery_ota.c`),
which writes the application region directly. On it:

- `STAGED` is stored before the first page is erased. A transfer that stops,
  even after the last page, leaves `STAGED`, and `tfw-boot` refuses to boot it.
- A verified `OTA_END` is `PENDING` at once and armed in the same write:
  `STAGED` → `TRIAL`. The floor does not move.
- A reset that finds `TRIAL` writes `FAILED` and stays in recovery. There is
  no previous image to restore, so recovery is another OTA round, which
  `OTA_BEGIN` accepts.
- A reset that finds `CONFIRMED` evaluates the image, advances the floor to its
  version and writes `GOOD`, before the update request is served. `GOOD` waits
  for the authority to hold the floor: an element that refuses the counter
  write leaves `CONFIRMED`, the image boots without an update session, and the
  next reset retries the advance.
- Every reset reads the floor from its authority before anything is evaluated,
  except a `TRIAL` failed at reset, which evaluates nothing. Where there is no
  element the authority is the EEPROM copy. An element that is there and fails
  the read is not covered by that copy: nothing is verified and nothing runs,
  the boot lands in recovery with `TFW_ERR_CRYPTO`, every image an update
  delivers is refused with the same status while the read fails, and the block
  is not written -- a `CONFIRMED` is settled at the first reset that reads the
  counter. `RESET` from recovery reads it again.
- Recovery refuses `OTA_BEGIN` with `TFW_ERR_BAD_STATE` while the block is
  `CONFIRMED`. The first page would replace `CONFIRMED` with `STAGED`, and the
  confirmed version's floor advance would be lost: an older signed image could
  then be accepted once the counter read recovered. The refusal writes nothing,
  so a reset leaves `CONFIRMED` and settles it; a transfer is accepted once that
  settle has written `GOOD`. The way forward is a reset (`RESET` from recovery),
  not a retried transfer.

What a reset in the middle of each write leaves:

| Write | Reset before it | Reset during it | Reset after it |
|---|---|---|---|
| `STAGED`, before the first page | the old state, region untouched | a torn block | `STAGED`: recovery |
| `TRIAL`, after a verified `OTA_END` or a pending image at reset | `STAGED` or `PENDING`: recovery, or tried again | a torn block | `TRIAL`: the next reset fails it |
| `FAILED`, a trial found at reset | `TRIAL`: failed again | a torn block | `FAILED`: recovery |
| settle, element tier: counter, then `GOOD` | `CONFIRMED`: settled again | the counter is the element's, carried on from where it stopped; the block is torn | between the two, `CONFIRMED` with the counter advanced: the advance is no step, only `GOOD` is written; a refused counter write writes nothing: `CONFIRMED`, settled again; a refused counter read writes nothing either: recovery, `CONFIRMED`, where `OTA_BEGIN` is refused until a reset settles it |
| settle, software tier: floor and `GOOD` in one block | `CONFIRMED`: settled again | a torn block | `GOOD` |

A torn block fails its CRC, and the next reset finds no usable platform block:
recovery, with no pinned key, so nothing verifies until a programmer rewrites
the region. Recovery never writes the zeroed block it was given over the torn
one. Closing that window needs the state outside the block's CRC or in its own
record, which is open question 10.

### The manifest gate (#78)

The application, never `tfw-boot`, reads the SUIT envelope and refuses to
activate an image whose manifest does not hold. `src/core/suit.{c,h}` is the
reader and the gate; `tfw-boot` is unchanged. The reader is iterative, bounds
every length by what remains and by the profile, and refuses any member the
profile does not list:

```
107({2: bstr .cbor [bstr .cbor [-16, bstr32],
                    bstr .cbor 18([h'A10126', {} / {4: bstr<=32}, nil, bstr64])],
     3: bstr .cbor {1: 1, 2: seq, 3: bstr .cbor {2: [[1..4 bstr<=16]],
                                                  4: bstr .cbor [20, {1: vendor-id,
                                                                      2: class-id,
                                                                      3: bstr .cbor [-16, bstr32],
                                                                      14: image-size}]}}})
```

It authenticates before it parses: the manifest digest over the bstr-wrapped
manifest, then ES256 over the Sig_structure
`["Signature1", h'A10126', h'', <the digest member>]`, verified by the element
against the board id's one image key in slot 2, the key `tfw-boot` already
uses (`tfw_crypto_verify_image`). No new crypto library. The gate writes
`VERIFIED` when every check holds and `FAILED` otherwise, so
`tfw_boot_trial_activate` refuses with `TFW_ERR_BAD_STATE` and no boot request
is written. Existing status codes only:

| Refused | Status |
|---|---|
| truncated; a length past what remains or past its bound; an 8-byte argument; an envelope over 320 bytes | `TFW_ERR_RANGE` (11) |
| a wrong type, tag, key or algorithm; an unlisted or extra member; trailing bytes; an indefinite length | `TFW_ERR_UNSUPPORTED` (7) |
| the manifest digest or the signature | `TFW_ERR_CRYPTO` (14) |
| the vendor id, the class id against the board id, an unbound class id, the trailer's `board_id` | `TFW_ERR_UNSUPPORTED` |
| the image digest against the staged image or the trailer's measurement | `TFW_ERR_CRYPTO` |
| the size against the staged length or the trailer's `image_len` | `TFW_ERR_RANGE` |
| the sequence number against the trailer's `image_version` | `TFW_ERR_PROTECTED_REGION` (16) |

The vendor id is UUIDv5(URL, `https://github.com/jeremyhahn/trusted-firmware`)
in `include/tfw/config.h`; each class id is UUIDv5(vendor, board name) in the
board's `board_profile.h`. The build emits the bytes to sign: `make suit-tbs
IMAGE_VERSION=<n>` writes `.suit-manifest` and `.suit-tbs` beside the image,
and `make suit-envelope SUIT_SIG=<r||s>` wraps go-xkms's signature. No signing
code is in this tree. The vectors are literals in `testdata/suit_vectors.h`.

Only the ATmega2560 on the element tier links the reader (`OTA_MANIFEST := 1`
in its `profile.mk`; `mk/firmware.mk` adds `src/core/suit.c` to the object
list). The 328P and 32U4 keep the trailer alone under their 16 384-byte line,
and the software tier has no stored key to verify against.

Nothing calls the gate yet: the stager (below) does not, and there is no
`OTA_ACTIVATE`, so `--gc-sections` drops it and no image grows. Carriage, the
envelope after the image in the same `OTA_DATA` stream and counted in
`OTA_BEGIN`'s total, is open question 5 and is not wired.

### The application stager (#75)

An image with an external store links `src/core/ota_stage.c` and the store's
driver, `src/store/spi_nor.c`. It answers the four session commands tfw-boot
answers, with the same payloads, states and refusals, and puts the bytes in
the store's `STAGE` area instead of application flash. The build selects it
by object list. `OTA_STAGE` defaults to `ETH` on a board whose `profile.mk`
names an `OTA_STORE`. The 328P, 32U4 and 2560 name `spi_nor`, and the 32U4,
having no Ethernet, builds without it unless `OTA_STAGE=1`.

- `OTA_BEGIN` checks the length first: it must exceed the trailer, fit
  `STAGE`, and be whole 256-byte pages. Then it erases the `MARK` sector, so a
  record from an earlier round is gone before a byte of `STAGE` changes.
- `OTA_DATA` takes the next offset only and erases each sector as the stream
  enters it.
- `OTA_END` needs every byte. It reads the trailer and checks the magic,
  version, board id and image length, then hashes the body and compares the
  hash with the trailer's measurement. A mismatch is `ERR_CRYPTO` and the
  session is `FAILED`. A match writes the `MARK` record,
  `[magic 'TFSG'][total_len:4][measurement:32]`, with the magic programmed
  last, so a record cut short by a reset reads as no record.
- These are the stager's checks only. Signature, version floor and manifest
  are tfw-boot's gate, which reads `MARK` and verifies `STAGE` again before it
  copies anything. The store is outside the trust boundary.

The chip select is D8 on every AVR board. It is the proposal from open
question 2, not a ruling, and is a single line in each `board_profile.h`. An
image that links the stager reserves the pin, as an Ethernet image reserves
the shield's.

`tfwsim` links the stager with `test/model/spi_nor_model.c`, a software
25-series part, as the store. `make test` runs the stager against the model.
The SDK's `StageImage` stages over serial and TCP in
`sdk/go/tfwsim/ota_test.go`.

### tfw-boot's copy of STAGE (#75)

A tfw-boot built with `BOOT_STORE=1` links `boot/boot_stage.c` and the store's
read-only bus, `boot/hal/avr/boot_ota_store_avr.c`. `main()` calls
`tfw_boot_stage_take` once per reset, after the platform block settles and
before a session or the `STAGED` check. The contract is in `boot/boot_stage.h`.

- **The mark.** The stager's `MARK` record names the staged stream. With no
  valid record, or a length that does not fit `STAGE` or the application
  region, nothing is copied and the boot goes on as before.
- **The re-check.** The staged trailer must parse, be bound to this board
  (`tfw_boot_binding_permit`) and not be below the floor
  (`tfw_boot_rollback_permit`). These are the same checks the gate makes. The
  SHA-256 over `image_len` staged bytes, through `tfw_boot_measure_from` on
  the build's own hash backend, must equal both the trailer's measurement and
  the mark's. The signature is not checked here: `tfw_boot_evaluate` checks
  it on the copied image, as on any other.
- **The copy.** Page by page through `tfw_boot_flash_write_page`. The body's
  pages go first; the page holding the trailer is written last. Until that
  page is written the region has no trailer for the new image, so a cut
  anywhere earlier leaves nothing that parses as it. On the 2560, `STAGE`
  (64 KB) is smaller than the region, so the trailer lands at the region's
  end, past the end of `STAGE`.
- **The hand-over.** `STAGED` is written before the first page. `GOOD` with
  `ota_state` `PENDING` is written after the last, so the image goes to the
  one trial boot (#82, `boot/boot_trial.c`, above) exactly as a session's
  verified `OTA_END` does. A fresh image is taken only where
  `tfw_boot_trial_begin_permit` allows a new round. An image that is already
  installed (its trailer equals the staged one) is not copied again.
- **An interrupted copy.** A reset mid-copy leaves `STAGED`. The next boot
  copies again from the first page: the store is never written by the boot, so
  the source is still there. This is resumed whatever the trial state is.

`test/unit/boot/test_boot_stage.c` runs this against the NOR model on the
mock's store bus, twice: under the 328P's profile, and as
`test_boot_stage_2560`, which includes an image that ends exactly at `STAGE`'s
end. The tests cover a valid copy into its trial; a bad hash, a wrong board
id, a missing mark and a version below the floor, each of which copies
nothing; and a copy cut at a chosen page that recovers on the next boot.

**`BOOT_STORE`** (`mk/boot.mk`) is its own setting and defaults to off, like
`BOOT_OTA`. The board's `OTA_STORE` says only that the part is fitted. Off,
`boot/boot_stage_none.c` is linked instead, and LTO folds its one call away.
It is the sixth field of a `BOOT_SUPPORTED` row
([engineering.md](../secure-boot/engineering.md)). Only the 2560 without OTA
in boot has it on (5 332 / 8 192 flash at 6800776). The 328P and 32U4 do not:
their sections are 4 096 bytes with 3 812 and 3 882 used, and the copy path
costs more than the margin.

**The 2560's swap-move** (`boot/boot_swap.c`). The application region is the
primary slot: 64 KB at `0x00000`, the trailer at `0xFF80`, and one 256-byte
move unit above it. The secondary is at `0x10100`. Both are board data in the
2560's `board_profile.h` (`TFW_SWAP_*`, open question 7). There is no backup
area, because the other slot holds the fallback. The staged image is copied
into the secondary (`TFW_OTA_STAGE_DEST`), and the primary is untouched until
the swap. The move shifts the primary up a unit, top first. Then each unit
swaps in two writes, and each write's source survives the step after it.

Progress is journalled in two five-byte copies at EEPROM `0x0E0`–`0x0E9`
(open question 10). Each copy holds the phase, the step, a sequence number and
a CRC-16 over the three. The phase also says which way the swap runs and, once
a forward swap ends, that the slots are swapped. Every advance writes the copy
not holding the current position, reads both back and checks that the new one
is now the newest valid copy. A reset that tears an EEPROM byte can leave a
value that is neither the old one nor the new one. That copy then fails its
CRC, and the other still names the step before, which the next boot resumes
from. If neither copy is valid, the position cannot be read, and the record
reads idle: nothing is moved on a guess. The gate judges the primary as it
stands, and a half-moved primary is refused into recovery, where a new image
swaps in whole.

The journal makes the 2560's bootloader-owned EEPROM reach past the wire's
`EEPROMReservedEnd` (`0xC0`). The rows `0x0E0`–`0x0E9` are the board profile's
`TFW_BOOT_EE_EXTRA_BASE` and `TFW_BOOT_EE_EXTRA_LEN`, and the firmware refuses
host writes there: `EEPROM_WRITE` answers `TFW_ERR_PROTECTED_REGION`, and
`EEPROM_READ` takes the same auth gate as the platform block
(`src/caps/storage.c`). Raising the bound to cover the journal is with the
owner (the ADR's open question 12).

`main()` makes three calls around the trial. Each changes nothing on a board
without the slots, where `boot_swap_none.c` is linked.

- **At reset:** a swap that a reset cut short is finished before anything
  reads a slot. If the slots are swapped:
  - a trial that ended unconfirmed falls back to the image before it, not to
    recovery;
  - a confirmed trial is kept, and the record goes idle before `GOOD` is
    written.
- **Before the gate:** a pending image is checked in the secondary first, by
  `tfw_boot_evaluate_at` at the secondary's base: the same trailer, board id,
  floor, measurement and signature checks as the primary's gate, with no
  second parser or hash. A refused image is never swapped: the block goes
  `GOOD` with `FAILED`, and the primary keeps running. A verified one is
  swapped into the primary, where `tfw_boot_evaluate` checks it again.
- **Refused, or on a failed trial:** the block is written first, `GOOD` with
  `FAILED` (`tfw_boot_trial_fall_back`, under fail's guard). Then the slots
  swap back. A reset between the two finds the block `GOOD` and the slots
  swapped, and runs the swap back then.

The serial session in the boot writes the secondary too, from
`TFW_OTA_STAGE_DEST`, so the primary is untouched while an image arrives. At a
whole `OTA_END`, `tfw_boot_swap_commit` stores `PENDING` with the primary's
image still `GOOD`, which is the state the store's copy leaves. It then checks
the image in the secondary, swaps it in, and `tfw_boot_evaluate` checks it
where it runs:

- **Refused in the secondary:** no swap page is written, the block goes
  `GOOD` with `FAILED`, and the primary boots.
- **Verified:** the trial is armed, as before.
- **Refused after the swap:** the slots swap back, and the previous image
  boots.
- **Reset from the commit on:** the next boot follows `main()`'s path. The
  image is checked again or the swap resumes, and the image goes to its one
  trial.

On a board with the slots the session does not store `STAGED`: the primary is
whole while an image arrives, so a reset during a transfer is an ordinary boot
through the primary's gate, not recovery. Recovery stays for a primary that
fails its gate. `write_page` stores `STAGED` only where `TFW_OTA_STAGE_DEST` is
the primary's base.

`mk/boot.mk` links `boot_swap.c` where the board has the slots (`BOOT_AB` in
`profile.mk`) and an image can land in the secondary. On the 2560 that is the
`BOOT_OTA=1` row and the `BOOT_STORE=1` row. Every other row and board links
the stub, whose commit does nothing, so a single-slot session writes in place
as before.

`test/unit/boot/test_boot_swap.c` (2560 profile) covers:

- a full swap;
- a reset cut at each of its 1 536 page writes;
- a rollback, and a cut during it;
- images that fill the slot;
- a boot that resumes a swap cut in each phase;
- a confirmed trial that keeps the swap;
- a failed trial that swaps back;
- a fall-back cut short and finished at the next boot;
- an image the gate refuses after the swap;
- an image refused in the secondary, which writes no swap page;
- a session cut short, which leaves the secondary written, the primary
  untouched, and a reset that boots the primary;
- a session image that is checked, then swapped, then tried (the secondary's
  check sees only the session's 2 page writes; the total is 2 + 1 536);
- a session image refused in the secondary: the session's 2 page writes and
  no swap;
- a session whose swap is cut, then a boot that finishes the swap and enters
  the trial.

No size has been measured, because there is no avr-gcc on the dev host. By
arithmetic, the store row is about 6 000 / 8 192: 5 332, plus the ADR's +424,
plus about 200 for the three calls and the second slot check. The
`BOOT_OTA=1` row is about 7 650 / 8 192: 6 942, plus the +424 swap core, plus
about 200 for the calls and the commit, plus about 50 for the secondary's
check (a flash reader, the trailer address and the call before the swap).
The secondary's check reuses the gate, so a signed update is verified twice:
once in the secondary and once in the primary. Both rows are measured on a host with
avr-gcc.

#### Open (for the CA and owner; not decided here)

- **The selects.** The store's select is D8 on every AVR board, as open
  question 2 proposes. The W5x00's (D10) and the SD socket's (D4) are held
  high while the boot has the bus open. Each is one `*_CS_IO` line in
  `board_profile.h`. Options:
  - rule D8 as proposed;
  - move it per board, which is a one-line change each.
- **A stale mark after a UART update.** The boot never writes the store, so a
  mark stays until the application's next `OTA_BEGIN` erases it. Suppose an
  image is installed over the boot's own session (UART) and the old staged
  image is still marked. That image is then copied back over the new one, as
  long as its version is not below the floor. The rollback floor is the only
  thing that stops it today. Options:
  - the boot's session erases or invalidates `MARK` when it commits, which is
    a store write in the boot;
  - `MARK` carries a round counter that the platform block records;
  - the boot takes a staged image only when the platform block says the
    application staged one;
  - A/B with swap-move, which replaces the single-slot copy. This does not
    close it: the copy into the secondary still reads `MARK`. A staged image
    that either slot already holds is not taken, so an image that failed and
    was swapped back is not tried again. It still cannot happen on any row:
    the `BOOT_OTA=1` row has a session and no store, so nothing reads `MARK`,
    and the store row has no session in the boot. It comes back with any row
    that has both, and OTA plus the store does not fit (below).
- **The store on the 4 KB parts.** It does not fit next to the element tier.
  Options:
  - The application copies before the reset. Code outside the boot section
    cannot program flash on AVR, so this needs a small page-write entry point
    in the boot section. The boot still evaluates the copied image at the next
    reset.
  - The store only on the 2560.
  - A smaller signature tier. There is none below the element path these rows
    already use.
- **The 2560 with OTA and the store.** It was 8 548 / 8 192 before the size
  slice and has not been measured since, so it is not a row. By arithmetic,
  not measured: the size slice took 80 bytes off the store-on row, and the
  ADR puts swap-move at +424, so about 8 892. That is over the section even
  with the shrink. Options:
  - measure it at 6800776 and add the row if it fits;
  - choose one update path per image, as the application does with its
    transport.

Not yet: the 328P/32U4 `PREV` restore;
the stager carrying the envelope and calling the manifest gate;
`OTA_CONFIRM` and `OTA_ACTIVATE` on the wire (decision 8's ids are
proposed); keeping the failed measurement (open question 10); and the trial
deadline and watchdog (`TFW_OTA_TRIAL_TIMEOUT_S` is a board value, open
question 6, and the application's `.init3` disarms the watchdog today).

## Trust boundaries

- **The signature, the board id and the floor are the only things the device
  trusts.** The transport, the store, the control plane and the external SPI
  store are not trusted for integrity: any byte of the image can be wrong, and
  the device refuses it. The control plane is trusted only to choose which
  signed image a device runs, and to confirm it.
- **The bootloader is the authority.** Nothing the application decided about an
  image survives into the boot decision except the slot states, and every image
  is evaluated again before it runs.
- **The external store is outside the boundary.** The boot copies from it and
  then evaluates the internal copy; it never executes from it. A compromised
  application can corrupt `PREV`. On the 328P that leaves no runnable image, and
  recovery needs a programmer; on the 2560 the fallback is the other internal
  slot, which the application cannot write.
- **The bootloader is never updated over the wire.** The trailer format a
  deployed `tfw-boot` accepts is fixed for that device's life. Adding the
  board-id check, A/B or the store path is a bench reflash.
- **Anti-rollback is as strong as its counter.** Every AVR board ships the
  element tier, whose monotonic counter holds against code on the device. A soft
  EEPROM floor would stop only a replayed old image arriving over the wire.
- **Activation changes what runs.** `OTA_ACTIVATE` and `OTA_CONFIRM` need the
  operator auth session on the element tier, over Ethernet as over serial. The
  control plane sends them only for a principal holding `ota.activate` or
  `ota.confirm` in the device's tenant.
- **The element's I²C bus** carries the measure and verify results. An
  interposer is the element tier's known risk, as it is on every boot today.

## Alternatives rejected

| Alternative | Rejected because |
|---|---|
| The device verifies the CoRIM | The CoRIM is the verifier's artifact. The device verifies a SUIT subset carrying the same four facts. |
| An SPM entry point in the AVR boot section so the application can stage an image | It voids the BLB1 mode 3 read barrier from inside: the application could reach boot-section code. The secure-boot architecture forbids it, for the same reason it forbids a CDI trampoline. |
| The W5x00 session in the AVR bootloader | Measured: it does not fit either boot section (ADR 6.1). |
| A serial-attached host doing the network side | Every board takes its image over the network itself. |
| A scratch-page swap for the 2560's A/B | One scratch page is erased once per page swapped. A 64 KB slot is 256 pages, and AVR flash is rated for 10 000 erase cycles, so the scratch page wears out in about forty updates. Swap-move shifts the slot by one page instead and wears every page about twice per update. |
| Advancing the floor on the first boot of a new image | The fallback image would then be below the floor and refused, so the device would have nothing to fall back to. |
| More than one trial, or a device-local confirm | One trial; a failure means another OTA round. Confirm follows the control plane's appraisal of the new image's attestation. |
| A pull model: the device downloads from a URL | The device has no HTTP client and no TLS, and it never chooses its own image: that is fleet policy, which is go-trusted-platform's. |
| A store, registry or update server in this repo | go-quicbaas owns the store and go-trusted-platform the endpoint and rollout; a second one here is the duplication L1 forbids. |

## What this deliberately does not do

- **No delta updates.** A delta needs the old image readable at the destination
  and a patcher in the trusted computing base. Whole images only.
- **No encryption of the image.** Confidentiality of firmware is the platform's
  flash-encryption feature where a part has one (ESP32), not this format's. The
  manifest profile therefore takes only the authentication half of SUIT's MTI
  profile.
- **No bootloader update over the wire**, on any class.
- **No partial updates of the application alone.** The image is one
  measurement, firmware and application together.
- **No rollout logic on the device.** The device does not decide when to
  update; it accepts or refuses what it is sent, and the control plane confirms.
