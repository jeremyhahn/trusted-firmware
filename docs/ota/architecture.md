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
> check (#76), the trial state machine (#77), the manifest gate (#78), or the
> application stager, tfw-boot's copy of `STAGE` and the 2560's swap-move (#75).
> The SPEC row `ota` is `in progress`.
>
> For the code on main: [engineering.md](engineering.md) (modules, gating,
> tests), [api.md](api.md) (the wire and `sdk/go`) and [user.md](user.md)
> (staging an image on a 2560).

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
| ATmega2560 | serial session, swap-move, trial: 7 954 of 8 192; or store read, swap-move, trial: 6 492 of 8 192 (verify-measured, with the confirm request). Both together were 8 916 before the confirm request, which does not fit (element tier, measured with `ARCH_BOOT_OPT_CFLAGS`; 9 110 with `-mcall-prologues` alone, 9 616 before) | `ETH=1`, stager, manifest reader: about 24 445 of 65 536 | both |
| ATmega328P | verify-and-copy, trial: 3 924 of 4 096; the store row `atmega328p:atecc608a:atecc608a:atecc608a:0:1`, without the `PREV` restore (owner queue 176, option (a)): at `cb104cf4`, `make bootloader-check` (avr-gcc 14.2.0) found it overflows its 4096-byte boot section (the part's largest `BOOTSZ`) by 1498 bytes, while the base row fits at 3712 / 4096; it is not in `BOOT_SUPPORTED`, and activation on the 328P goes back to the owner (#52); with the restore it was 5 982, over | `ETH=1`, stager: about 19 267, and about 22 153 with the manifest reader | both, under the 28 672 region budget (proposed); none under the 16 384 policy line |
| ATmega32U4 | as the 328P, unmeasured; with the restore, row `atmega32u4:atecc608a:atecc608a:atecc608a:0:1`: candidate, unmeasured (`make bootloader-candidate BOOT_CANDIDATE=atmega32u4:atecc608a:atecc608a:atecc608a:0:1`) | no Ethernet controller wired yet | follow-up |

## Device state machine

### Slot states

Every class reports the same states through `OTA_INFO`. On AVR they are encoded
in the platform block's `boot_state` and `ota_state`, extended and never
renumbered (ADR; open question 10 is closed by item 56, owner ruling
2026-10-17, CA m499). The bootloader owns the transitions marked
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
| `FAILED` | refused, or the trial ended unconfirmed; the failed measurement is not kept in the platform block | overwritten by the next `OTA_BEGIN` |

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
             tfw_app_after_update refused ─► reset at once (a failed trial)
             init refused, or the descriptor failed its checks ─► reset at
             once (a failed trial; tfw_app_after_update is not asked)
             ─OTA_CONFIRM─► CONFIRMED, signature recorded; the deadline
             still runs (deadline expired ─► reset, confirmed or not)
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
- **Authorization** (CA m502 item 2, 2026-10-17, #137; design, code in
  slice 2). `OTA_ACTIVATE` and `OTA_CONFIRM` carry a COSE_Sign1 ES256 signature
  by the row key ([api.md](api.md), "Authorization by the row key"), and
  tfw-boot checks it at the next reset, option (ii), with the verifier and key
  that check the image. The application writes the activate signature into the
  store beside the `MARK`, and the confirm signature into the confirm request
  in EEPROM, which grows by 64 bytes (the address is slice 2's, with the EEPROM
  map follow-up #125). tfw-boot rebuilds the payload from the trailer and the
  floor; a `MARK` whose signature fails is cleared and reported `FAILED`, as a
  refused image is, and a confirm request whose signature fails is no confirm,
  so the trial rolls back. `OTA_CONFIRM` stores the signature `OTA_AUTH` held in
the 75-byte confirm request (`src/core/ota_ctl.c`, #231), and tfw-boot settles
the request only when its magic, CRC and measurement match and that signature
verifies over the Sig_structure it rebuilds from the primary's trailer, with
the image path's key and verifier (`boot/boot_swap.c`, `confirm_requested`).
- **The confirm keeps the trial deadline** (PR #139 review, P1). An
  application that cannot verify the confirm signature itself must not end the
  trial on it: `OTA_AUTH` takes any 64 bytes, so a confirm that stopped the
  deadline would let an unauthenticated client keep an unattested image running
  for ever. Under option (ii) no row's application verifies (the element tier's
  verify is tfw-boot's call into the element too), so on every row `OTA_CONFIRM`
  writes the confirm request and does **not** stop the deadline; the confirm
  handler calls no trial function, and `tfw_ota_trial_stop` is gone
  (`src/core/ota_trial.c`). `tfw_ota_trial_poll` goes on as in any trial: it pets the watchdog
  until the deadline and then stops, and the watchdog resets into tfw-boot
  within one 8 s period. The trial therefore ends in a reset either way, the
  deadline's or an earlier `RESET` from the control plane, and that boot
  verifies the recorded confirmation: valid, the image is `CONFIRMED` and
  settles to `GOOD`; invalid or absent, it falls back as an unconfirmed trial.
  A row whose application verified the signature before recording it could stop
  the deadline at the confirm, as the pre-#137 handler does; no row does today.
- Option (i), the application verifying at the
  command, was refused: the soft provider has no application verifier
  (`src/crypto/soft/provider.c:258`), linking micro-ecc into the application
  costs kilobytes on the 4 KB parts, and a confirm verify at UECC_OPT 0 cannot
  finish inside the 60 s trial deadline (`TFW_OTA_TRIAL_TIMEOUT_S`). The cost of
  (ii), all estimates (the lane image has no AVR toolchain; slice 2 measures):

  | Row | Boot flash | Fits | Time at the settling boot |
  |---|---|---|---|
  | 2560 soft base (UECC_OPT 0) | about 250 B: the Sig_structure prefix, the payload encoding, one SHA-256 and verify call, the signature read | only once item 79's soft base is engineered to fit 8192 B; it does not link today (`docs/development/isp-route.md:195`) | one more P-256 verify, about 303 s more, so about 606 s at the activation boot and 303 s more at the confirm-settling boot; every other boot unchanged |
  | 2560 element tier | about 200 B; the verify is the element's | 8192 B: yes on the supported row (`docs/development/isp-route.md:216`, 7832 B) | tens of ms |
  | 328P and 32U4, 4 KB boot (trailer-only) | about 250 B | not established; the 328P has no soft row (owner ruling 2026-10-17 (m522)) | as the 2560 at the same UECC_OPT |
- **One trial.** The trial state is written before the jump, so any reset in
  it, whether the deadline, the watchdog, a crash or a power cut, rolls back.
  There is no attempt counter. The same image is not activated again: at a
  fallback, before the fallback itself, `tfw-boot` clears the store's `MARK`
  (owner ruling 2026-10-17, item 56; `tfw_boot_stage_forget`,
  `boot/boot_stage.c`), so no later reset copies the failed image back in. The
  clear programs the `MARK`'s magic to zero, so a clear a reset cuts short
  leaves no `MARK` either.
- **The timeout** is per board, `TFW_OTA_TRIAL_TIMEOUT_S` in
  `boards/*/board_profile.h`, 60 s on each AVR board with a boot section
  (ADR, "Where the timeout is configured"). The application
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
| `tfw_boot_trial_at_reset` | picks, in order: `TRIAL` → fail, `CONFIRMED` → settle, `PENDING` → try, else boot; on the 2560, `TRIAL` with a valid confirm request (ADR decision 8) → confirm, then settle | never |
| `tfw_boot_trial_arm` | `PENDING` → `TRIAL`, before the jump | nothing pending; a trial running; a confirm not yet settled |
| `tfw_boot_trial_fail` | `TRIAL` or `PENDING` → `TRIAL`, `FAILED`, so every later reset fails again; a pending image refused at evaluation never boots, even once a transient failure clears | no trial and nothing pending |
| `tfw_boot_trial_confirm` | `TRIAL` → `CONFIRMED`, called only by tfw-boot at reset, from `OTA_CONFIRM`'s request record | outside a trial; a trial already failed |
| `tfw_boot_trial_settle` | `CONFIRMED` → `GOOD`, floor := version | no confirm |
| `tfw_boot_trial_activate` | `VERIFIED` → `PENDING`, from `OTA_ACTIVATE` after it re-verifies the image and writes the MARK; `OTA_END` alone leaves `VERIFIED` | no verified image; a trial running; the measurement that last failed |
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
one. Open question 10 is answered by item 56 (ruling 72: owner ruling 2026-10-17,
CA m499): the store MARK is cleared at fallback, so the failed
image is never copied back in, and the failed measurement is not kept in the
platform block.

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
| the sequence number against the trailer's `image_version`, or below the platform's rollback floor | `TFW_ERR_PROTECTED_REGION` (16) |

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

On AVR the application calls the gate at `OTA_ACTIVATE`
(`src/core/ota_activate.c`, through `tfw_ota_manifest` in
`src/core/ota_manifest.c`), after the state checks and before the
authorization or the `MARK` is written, so a refused manifest leaves the store
as `OTA_END` left it (owner ruling 121, the split gate, CA m957, 2026-10-21; it
amends ruling 3, CA m499, which put the check in `tfw-boot`, where the 4 KB
parts cannot fit it). `tfw-boot` keeps the P-256 verify over the image and the
trailer on every row. A board that does not link the reader links
`src/core/ota_manifest_none.c`, which answers `TFW_OK`, so `--gc-sections`
drops the gate and its image does not grow. The ESP32's layer 0 calls the same
gate, after the trailer, at the trial boot (`boot/hal/esp32/l0_gate.c`).

Carriage (ruling 109, 2026-10-22; ADR decision 3, open question 5 settled): the
stream carries the envelope after the image, in the padding before the
trailer, as a little-endian u16 length then the envelope, and `OTA_BEGIN`'s
total counts it. Erased padding reads as length 0xFFFF and is refused with
`TFW_ERR_RANGE`, as is a length of zero or one past the padding. `make
image-seal` writes it there from `make suit-envelope`'s `.suit` on a board that
checks, and refuses to run without one; the region `tfw-boot` reads is
unchanged. No new command id.

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
- These are the stager's checks only. Signature and version floor are
  tfw-boot's gate, which reads `MARK` and verifies `STAGE` again before it
  copies anything. On a board that links the reader, `OTA_ACTIVATE` checks the
  manifest first, its sequence against the floor included (above), and a
  refusal there writes nothing. The store is outside the trust boundary.

The chip select is D8 on every AVR board. It is the proposal from open
question 2, not a ruling, and is a single line in each `board_profile.h`. An
image that links the stager reserves the pin, as an Ethernet image reserves
the shield's.

`tfwsim` links the stager with `test/model/spi_nor_model.c`, a software
25-series part, as the store. `make test` runs the stager against the model.
The SDK's `StageImage` stages over serial and TCP in
`sdk/go/cmd/tfwctl/e2e_ota_test.go`.

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
in boot has it on (6 302 / 8 192 flash, with swap-move). The 328P and 32U4 do
not: their sections are 4 096 bytes with 3 758 and 3 828 used, and the copy path
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
(item 56, ruling 72: owner ruling 2026-10-17, CA m499).
Each copy holds the phase, the step, a sequence number and
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

The journal (`0x0E0`–`0x0E9`) and the confirm request (`0x0EA`–`0x134`) lie
inside tfw-boot's EEPROM, which ends at the wire's `EEPROMReservedEnd`
(`0x140`) on every board (#125). The firmware refuses host writes there:
`EEPROM_WRITE` answers `TFW_ERR_PROTECTED_REGION`, and `EEPROM_READ` takes the
same auth gate as the platform block (`src/caps/storage.c`).

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

Measured (avr-gcc 14.2.0, `make bootloader-check`): the store row is 6 302 /
8 192 and the `BOOT_OTA=1` row 7 832 / 8 192. The `BOOT_OTA=1` row was 8 190
before issue #75's size slice built tfw-boot with `-mcall-prologues`
(`mk/arch/avr.mk`), which took 358 bytes off it and shrank every other row.
The secondary's check reuses the gate, so a signed update is verified twice:
once in the secondary and once in the primary.

#### Open (for the CA and owner; not decided here)

- **The selects.** The store's select is D8 on every AVR board, as open
  question 2 proposes. The W5x00's (D10) and the SD socket's (D4) are held
  high while the boot has the bus open. Each is one `*_CS_IO` line in
  `board_profile.h`. Options:
  - rule D8 as proposed;
  - move it per board, which is a one-line change each.

  Decided under ruling 109 (#52 comment 152690, 2026-10-26): D8 on every AVR
  board, as proposed.
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

  Decided under ruling 109 (#52 comment 152690, 2026-10-26): no current row has
  both a boot session and a store, and each image takes one update path
  (question 6), so nothing changes now. A future row with both takes the third
  option: the boot takes a staged image only when the platform block says the
  application staged one. That needs no store write in the boot and fails
  closed.
- **The store on the 4 KB parts.** It does not fit next to the element tier.
  Options:
  - The application copies before the reset. Code outside the boot section
    cannot program flash on AVR, so this needs a small page-write entry point
    in the boot section. The boot still evaluates the copied image at the next
    reset.
  - The store only on the 2560.
  - A smaller signature tier. There is none below the element path these rows
    already use.
- **The 2560 with OTA and the store.** It does not fit, so it is not a row.
  Measured with avr-gcc 14.2.0 and `ARCH_BOOT_OPT_CFLAGS` (`-mcall-prologues
  -mstrict-X -fno-tree-scev-cprop`), the full image linked at address zero
  from the objects `make $(BOOT_TARGET).elf BOARD=atmega2560 BOOT_OTA=1
  BOOT_STORE=1` builds, read as the build's size check reads it (the same link
  gives 7 666 for the `BOOT_OTA=1` row and 6 256 for the `BOOT_STORE=1` row,
  as the size check reports). `make bootloader` now refuses this configuration
  before it builds:
  - element tier (`atecc608a` for tier, measure and signature): 8 916 / 8 192,
    724 bytes over;
  - soft tier (`soft`, `soft`, `ed25519-donna`; item 79, owner ruling
    2026-10-17, makes the verifier P-256 on every row), at 03089f4: 96 492 / 8 192,
    88 300 bytes over.

  Size-only work with no change of behaviour (issue #75) took the element tier
  from 9 110 to 8 916: two code generation flags (60) and two smaller OTA
  session paths (134). What is left under that rule is worth tens of bytes, not
  724. With `-mcall-prologues` alone, at 03089f4: element tier 9 110. Before
  it, at c319f07: element tier 9 616, 1 424 bytes over; soft tier 98 204,
  90 012 bytes over. Neither fits within 8 192 either way.

  Each image takes one update path (question 6, decided 2026-10-26), as the
  application takes one transport, and the build says so by file selection:
  - `make bootloader` and `make boot-flash` refuse `BOOT_OTA=1 BOOT_STORE=1`
    at parse time (`mk/boot.mk`) with a message that names the two choices:
    `BOOT_OTA=1 BOOT_STORE=0`, OTA in tfw-boot, for an application built
    `OTA_STAGE=0`; or `BOOT_OTA=0 BOOT_STORE=1`, the external store, for an
    application built `OTA_STAGE=1`. `make bootloader-candidate` still
    measures the pair.
  - An application built `OTA_STAGE=1`, the Ethernet image's default, links
    `src/core/update_request_store.c` in place of `update_request.c`
    (`mk/firmware.mk`): it stages with `OTA_BEGIN`/`OTA_DATA`/`OTA_END` and
    answers `RESET(1)` with the existing `TFW_ERR_UNSUPPORTED`.
  - An application built `OTA_STAGE=0` links `update_request.c` and
    `ota_stage_none.c` and none of the store's files, so `RESET(1)` reaches
    the session and the store's commands answer `TFW_ERR_UNKNOWN_CMD`.

  `scripts/test_boot_onepath.py` pins the refusal and both object lists;
  `tfwsim` (the stage image) and `tfwsim-otaboot` (the image without the
  stager) answer each other's commands through `tfwctl`
  (`sdk/go/cmd/tfwctl/e2e_onepath_test.go`). The defaults do not pair: the
  2560's default tfw-boot is the `BOOT_OTA=1` row and its default application
  is the stage image, so build the two for the same path.

  **The store-copy boot and the trial, 2026-10-16.** What a `BOOT_STORE=1`
  boot does with a staged image, on main at b1c71f4. The tests are in
  `make boot-test`; the simavr suite runs this row too since #75's
  round slice (below).
  - (a) Trial: yes. The copy leaves the image `PENDING` in the secondary
    (`boot/boot_stage.c:224`), the swap takes it in (`boot/boot_main.c:263`)
    and it is armed `TRIAL` before the jump (`boot/boot_main.c:282`,
    `boot/boot_trial.c:34`). Pinned by `test_boot_swap`:
    `test_a_store_image_unconfirmed_swaps_back_and_is_not_retried`, from the
    real copy out of the NOR model.
  - (b) Confirm: the boot needs one, but nothing gives it yet. Only
    `CONFIRMED` settles (`boot/boot_trial.c:18`). Pinned by
    `test_a_confirmed_store_image_settles_and_is_not_copied_again`. Missing on
    every row, not this row alone: nothing in `boot/` or `src/` calls
    `tfw_boot_trial_confirm`, and `OTA_CONFIRM` is not on the wire (see "Not
    yet" below). So every trial on main ends unconfirmed. Estimate, not
    measured: 0 B of the boot section, which already links the transition. The
    cost is in the application, about 150 to 250 B for the command and the
    platform block's write.
  - (c) Rollback: yes. A reset that finds `TRIAL` (`boot/boot_trial.c:15`)
    swaps back to the image before it (`boot/boot_swap.c:229`, from
    `boot/boot_main.c:157`). The store's mark, still present, does not
    re-copy the failed image (`boot/boot_stage.c:186`). An image refused after
    the swap swaps back the same way (`boot/boot_main.c:265`). Pinned by
    `test_a_store_image_unconfirmed_swaps_back_and_is_not_retried` and
    `test_an_image_refused_after_the_swap_swaps_back`.
  - (d) Anti-rollback floor: yes. The image is checked before any page is
    copied (`boot/boot_stage.c:200`), in the secondary before the swap
    (`boot/boot_swap.c:251`) and by the gate in the primary. The floor moves
    only at settle (`boot/boot_main.c:205`). Pinned by
    `test_an_image_below_the_floor_copies_nothing` (`test_boot_stage_2560`),
    `test_a_store_image_below_the_floor_never_reaches_a_slot`, and by
    `test_settle_advances_the_floor_to_the_confirmed_version`
    (`test_boot_trial`). The gate's own floor check is pinned by
    `test_boot_policy`, which does not depend on the row.

### The 328P and 32U4 fallback from PREV (#52)

What runs today, on the host only. Neither half is on a board or under simavr
yet.

- **The backup, in the application.** An image that links the stager on a
  board without `BOOT_AB` also links `src/core/ota_prev.c` (`mk/firmware.mk`);
  the 2560 links `ota_prev_none.c`, since its other slot is the fallback.
  `OTA_BEGIN` erases `MARK`, then erases `PREV` and copies the application
  region into it through `tfw_hal_flash_read`, before any `OTA_DATA` is taken.
  `PREV` is laid out as the region itself, its trailer at the end.
  `test/unit/test_ota_stage.c` and `tfwsim` link the real backup; the
  slot-sized test counts `PREV`'s sector erases.
- **The restore, in tfw-boot.** A `BOOT_STORE=1` row without `BOOT_AB` and
  with `BOOT_PREV=1` (every such board but the 328P, below) links
  `boot/boot_prev.c` in place of `boot_swap_none.c` (`mk/boot.mk`). A `TRIAL`
  found at reset, and a pending image the gate refuses after the copy, both
  copy `PREV` back whole with the store's own copy loop
  (`tfw_boot_stage_copy`, trailer page last), then store `GOOD` with `FAILED`
  (`tfw_boot_trial_fall_back`). The block is written after the copy, so a reset
  mid-copy finds `TRIAL` or `PENDING` and copies again. `main()` evaluates the
  restored image as every boot; refused, it halts in `recovery_halt.c`, for the
  programmer. `test/unit/boot/test_boot_prev.c` (the host board's profile, which is the 328P's) covers a trial
  found at reset, a refused pending image, nothing to fall back from, a restore
  cut short and redone, and a store that does not answer.
- **Neither the 32U4's row nor the 328P's store row is a row.** The
  32U4's `BOOT_STORE=1` row is not in `BOOT_SUPPORTED`: its 4 096-byte section
  had 3 828 bytes used before the copy path (above), and with the restore it
  does not fit. Measured on 2026-10-23 on CI's image (avr-gcc 14.2.0), the core
  links at 5 966 bytes of flash against the 4 096-byte section on the 32U4,
  1 870 bytes over (ld: 1 696 over `text`), with 1 921 bytes of SRAM left
  (639 / 2 560 used), for `atmega32u4:atecc608a:atecc608a:atecc608a:0:1`. The
  same measurement of the 328P with the restore was 5 982 bytes, 1 886 bytes
  over (ld: 1 706 over `text`), with 1 409 bytes of SRAM left (639 / 2 048
  used); that is the row the ruling below drops. At `cb104cf4`,
  `make bootloader-check` (avr-gcc 14.2.0) found the 328P store row with the
  restore dropped overflows its 4096-byte boot section (the part's largest
  `BOOTSZ`) by 1498 bytes, while the base row fits at 3712 / 4096. BOOTSZ 4096 is already the largest setting on
  both parts, so there is nothing to raise. A row joins `BOOT_SUPPORTED` only
  once it fits and `bootloader-check` passes it; a candidate is measured on
  CI's image by `make bootloader-candidate BOOT_CANDIDATE=<row>`
  (`mk/boot.mk`), which builds the row without adding it and prints its size
  against the section, or ld's overflow. The 328P store row is therefore not in
  `BOOT_SUPPORTED`, and activation on the 328P goes back to the owner (#52). The 328P and 32U4 link `ota_trial_none.c` (below).
  `test/integration/simavr/boot_harness.c`'s `prev` row (an unconfirmed trial
  restores `PREV`; a refused pending image restores it; a corrupt `PREV`
  halts; each `GOOD`, `FAILED`) is not yet run, and the 328P no longer has it
  (`make integration-test-boot-prev` refuses the board): `BOARD=atmega32u4`
  runs it, outside `integration-test-boot` and CI.
- **Q1 is ruled: option (a) (owner queue 176, 2026-10-30).** The Company
  Agent's ruling (OD-4), verbatim: "The 328P tfw-boot keeps signature
  verification and activation and drops the PREV restore and confirm; no
  boot-time rollback on the 328P row, a failed activation is recovered by
  reflash; rollback stays on the 2560 and 32U4 rows where it fits; recorded as
  a row limit in docs/SPEC.md." No AVR row of tfw-boot links a manifest check
  (the manifest is checked by the application at `OTA_ACTIVATE`,
  `src/core/ota_manifest.c`), and both parts' applications already link
  `ota_manifest_none.c`, since only the 2560's `profile.mk` sets
  `OTA_MANIFEST`. What the store row adds to the boot is the store's copy
  (`boot_stage.c`), the activate authorization's Sig_structure and verify
  (`boot_cose.c`, `authorized()`) and the store's bus; the 328P row no longer
  adds `boot_prev.c`. Confirm was never on this row: `boot_prev.c`'s
  `tfw_boot_swap_at_reset` fails every `TRIAL` without reading a confirm
  request (only `boot_swap.c` reads one), and neither part's application links
  `OTA_CONFIRM`.
- **What the 328P store row links.** `boards/atmega328p/profile.mk` sets
  `BOOT_PREV ?= 0`, and the store row (`atmega328p:atecc608a:atecc608a:atecc608a:0:1`)
  links `boot_swap_none.c` in place of `boot_prev.c` (`mk/boot.mk`), with
  `boot_stage.c`, `boot_cose.c` and the store's bus unchanged. A `TRIAL` found
  at reset, and a pending image the gate refuses after the copy, have nothing
  to fall back to: `main()` ends the trial (`TRIAL` or `PENDING` to `FAILED`)
  and the part stops in `recovery_halt.c`; it is recovered by reflash. The row
  is not in `BOOT_SUPPORTED`: it measured 1498 bytes over its 4096-byte boot
  section (above). The 32U4 and the single-slot MPS2 rows keep
  `boot_prev.c`, the 2560 its swap-move. The 328P application still links
  `src/core/ota_prev.c` and backs the running image up in `OTA_BEGIN`; with no
  restore on this part that backup is not read by tfw-boot, and dropping it
  from the 328P application is a follow-up (`mk/firmware.mk`).

### The OTA round under simavr (#75, #77)

`make integration-test-boot` runs the real tfw-boot and the real application
image under simavr on two 2560 rows: `BOOT_OTA=1`, as before, and
`BOOT_STORE=1`, the store row (`boot_harness ... store`). On the store row the
NOR model (`test/model/spi_nor_model.c`) sits on the emulated SPI bus behind
D8, and the harness lays in it what the stager leaves: the signed image, padded
to pages, its trailer last, and the `MARK` record. The images are trailer v1
with the board id, signed by the harness's modelled element as every image in
that suite is. The application in that image is the real one, with the SDK
tick and `src/core/ota_trial.c` linked, so a trial left alone runs out at its
deadline on the emulated part, with no reset from the harness.

| Link of the round | Under simavr | Where else |
|---|---|---|
| `OTA_BEGIN`/`OTA_DATA`/`OTA_END` over TCP through the W5100 into the 2560's stager and store (`ETH=1`), then tfw-boot's copy and the trial | yes, `test_a_network_staged_image_runs_its_trial` (`boot_harness ... net`): the W5x00 model (`test/model/w5x00_model.c`) on the emulated SPI bus behind D10, beside the NOR part behind D8, and the harness the TCP peer through the model's peer calls; the W5100 only, the board's `ETH_CHIP` | `tfwsim`, `sdk/go/cmd/tfwctl/e2e_ota_test.go`, serial and TCP; the bench |
| the host's `(*Device).StageImage` over the emulated UART into the 2560's stager and store (`OTA_STAGE=1`), then tfw-boot's copy and the trial | yes, `TestOTAStageThroughTfwBoot` against the harness's bridge mode (`boot_harness ... bridge`); the trial state is asserted by the harness, not over the wire | `TestOTAStageAgainstFirmware`, on `tfwsim` |
| tfw-boot reads `MARK` and `STAGE`, checks, copies into the secondary, swaps in, arms `TRIAL`, the application boots | yes, `test_a_store_image_runs_its_trial_and_settles` | `test_boot_stage_2560`, `test_boot_swap` |
| `CONFIRMED` at reset: the floor advances to the version, `GOOD`; the mark is not copied again | yes, same test; the harness leaves `OTA_CONFIRM`'s request record and resets; tfw-boot confirms, settles and clears it | `test_boot_trial`, `test_boot_swap` |
| `OTA_ACTIVATE` and `OTA_CONFIRM` from the application | decision 8 proposed them; they landed on the wire at `0x6D` and `0x6E`, beside `OTA_INFO` at `0x6C` (`include/tfw/protocol.h`), `OTA_ACTIVATE` with `TFW_FEATURE_OTA_ACTIVATE` and the other two with `TFW_FEATURE_OTA_CTL` (`src/core/commands.def`), serial only (`src/proto/link_eth.c`); `boot_harness.c` sends `OTA_ACTIVATE` over the emulated UART | `test_ota_stage`, `test_ota_single`; `tfwsim`, `sdk/go/cmd/tfwctl/e2e_ota_test.go` |
| a trial not confirmed: the reset swaps back, `GOOD` `FAILED`, the marked image not taken again, the floor unmoved | yes, `test_an_unconfirmed_store_image_swaps_back`; the harness forces that reset | `test_boot_swap` |
| the trial deadline: nothing confirms, the application's `ota_trial.c` takes a watchdog reset at `TFW_OTA_TRIAL_TIMEOUT_S` (the board value, 60 s), WDRF in MCUSR at the reset edge and `TFW_RESET_WATCHDOG` in `SYS_INFO`, within one 8 s watchdog period of the deadline (`tfw_hal_reset` disarms the 8 s watchdog before its 15 ms one, which simavr would otherwise not apply); tfw-boot copies `PREV` back, `GOOD` `FAILED` | yes, `test_an_unconfirmed_store_image_rolls_back_at_the_deadline`, about 1.05 billion emulated cycles | `test_ota_trial`, `test_boot_trial` |
| a staged byte that fails the hash, an image below the floor: no page written, the primary boots | yes, `test_a_bad_store_image_copies_nothing` | `test_boot_stage_2560` |
| the store is only read by tfw-boot | yes: no erase, no program and no malformed transaction reach the model on any boot | — |
| 32U4 (not the 328P, which has no restore): a trial not confirmed or a pending image refused copies `PREV` back, `GOOD` `FAILED`; a corrupt `PREV` halts | no: the `prev` row (`make integration-test-boot-prev`) is not yet run and stays out of CI until the 32U4's `BOOT_STORE=1` row is measured and in `BOOT_SUPPORTED`; two of its three cases meet the re-take gap below | `test_boot_prev` |

The bench repeats the round with the W5x00, the NOR part and the element on a
2560; it is the only place the network leg meets a real controller, a real
network and the W5500.
The application's refusal of the failed image at `OTA_ACTIVATE`
(`tfw_boot_trial_activate`, `TFW_ERR_BAD_STATE`) is pinned by `test_boot_trial`;
the command itself is on the wire at `0x6D`, serial only.

Not yet: the 32U4 `PREV` restore on a board or under simavr (above; the 328P
has none, by the ruling);
the manifest gate at `OTA_ACTIVATE` under simavr or over `tfwsim` (it is
host-tested, `test_ota_manifest` and `test_suit`, and `tfwsim` links
`src/core/ota_manifest_none.c`);
and `OTA_ACTIVATE` and `OTA_CONFIRM` over TCP (serial only for now: none of
the control commands is on the Ethernet allow-list, and
`TestE2EOTAControlIsSerialOnly` pins each one's `TFW_ERR_UNKNOWN_CMD` over TCP
beside its serial answer on the same simulator). The failed measurement
is not kept: open question 10 is closed by item 56 (owner ruling 2026-10-17,
CA m499). `OTA_INFO`, `OTA_ACTIVATE` and `OTA_CONFIRM`
are on the wire at decision 8's ids, and `OTA_CONFIRM` leaves the trial
deadline running (#137). `tfwsim` drives the three end to end from `sdk/go`
and `tfwctl` (`sdk/go/cmd/tfwctl/e2e_ota_test.go`) with the platform block in
each state tfw-boot leaves: `OTA_INFO`'s `TRIAL` and fell-back flags,
`OTA_CONFIRM` taken in `TRIAL` and refused in `CONFIRMED` and after a fall-back,
`OTA_ACTIVATE` refused in `TRIAL` and `CONFIRMED`, and
`TFW_ERR_AUTH_REQUIRED` for either sent without `OTA_AUTH`.

The round through the real tfw-boot over `tfwsim`'s pty (#52). Given
`--boot-key FILE` (a 64-byte P-256 public key), and optionally `--app-image
FILE` and `--boot-floor N`, the software-tier `tfwsim` runs tfw-boot, built as
a 328P stage image's boot is (store on, the boot's own OTA session off, P-256
in software, one slot, `boot/hal/host` for its HAL), at power on and at every
`tfw_hal_reset`, keeping the EEPROM, the store and the handoff across it
(`tools/tfwsim/boot_host.c`). The element and auto simulators link
`tools/tfwsim/boot_none.c` and have no bootloader. `TestE2EOTARound*`
(`sdk/go/cmd/tfwctl/e2e_ota_round_test.go`) signs with a fresh key per test
and gives `tfwsim` only the public half: an activated image boots on trial
with the floor unmoved; an unconfirmed trial ends at the next reset in
recovery, `TFW_ERR_BAD_STATE`, as this row has no `PREV`; an image below the
floor is not taken and its `MARK` stays; an activation signed for another
measurement is refused at the take and its `MARK` forgotten.

The fallback flags. The diagram's `flags: refused` and `flags: fell back`
are the platform block's `flags` byte (`TFW_BOOT_EE_O_FLAGS`, inside the CRC)
and `ota_state` `FAILED`. The one bit defined is `TFW_BOOT_EE_FLAG_REFUSED`
(`include/tfw/boot.h`): the 32U4's fall-back (`boot/boot_prev.c`; the 328P
row links no restore)
sets it when the restore followed a refusal at evaluation and clears it when
it followed a trial left unconfirmed, in the same block write that stores
`GOOD` with `FAILED` (`tfw_boot_platform_store_flags`, `boot/boot_store.c`).
`OTA_INFO` reports it as flags bit 1 beside bit 0 (`src/core/ota_ctl.c`,
`SlotRefused` in `sdk/go`), as decision 8 defines them; with `ota_state` not
`FAILED` the bit is not reported. `test_boot_prev` pins both writes,
`test_boot_settle` that the byte survives the block's CRC and a later store,
and `test_ota_stage` the three answers. Two halves are still apart: `OTA_INFO`
is linked only into a `BOOT_AB` image with the stager (`mk/firmware.mk`), the
2560's, whose swap-back does not set the bit, so there bit 1 stays clear after
a refusal too; and a 328P or 32U4 image, whose tfw-boot writes it, serves no
`OTA_INFO`. Until one of them moves, only `tfwsim` reports bit 1.

What runs of the trial's two layers: `tfw-boot` arms the watchdog at its
longest period (8 s on AVR, `tfw_boot_watchdog_arm`) after `TRIAL` is written
and before the jump, on the trial path only, in `boot_main.c` and in the
recovery session. In the application, `src/core/ota_trial.c` reads the
platform block's `boot_state` before the application's `init`; in `TRIAL` it
re-arms the watchdog, and the superloop pets it and counts
`TFW_OTA_TRIAL_TIMEOUT_S` (60 s on each AVR board with a boot section, ADR
open question 6, settled), resetting into `tfw-boot` at expiry. The
application's `.init3` still disarms the watchdog on every boot, so the
boot's arm covers the window from the jump to `.init3`, and the re-arm covers
the rest of the trial. Only the 2560 (`BOOT_AB`) links `ota_trial.c`; the 328P
and 32U4 link `ota_trial_none.c` until their `BOOT_STORE=1` row, which can
write `TRIAL` and restore `PREV`, is in `BOOT_SUPPORTED`.

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
  application can corrupt `PREV`. On the 32U4 that leaves no runnable image, and
  recovery needs a programmer (the 328P row keeps no `PREV` to restore; a
  failed activation there is recovered by reflash); on the 2560 the fallback is the other internal
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
