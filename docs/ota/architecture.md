# OTA — architecture

> **Status: ACCEPTED (owner, 2026-10-15) where the ADR's decisions are
> ACCEPTED; PROPOSED where they are** · Issue: #52 · Scope: trusted-firmware
> (image format, device side, `sdk/go` upload helper); go-trusted-platform,
> go-quicbaas, go-xkms and qrdb (seams only) · Last verified: 2026-10-15
>
> Decisions, their reasons and the measured flash are in
> [adr-ota-image-format-and-trust.md](adr-ota-image-format-and-trust.md). No
> code exists for anything here that is not already shipped by
> [`secure-boot/`](../secure-boot/architecture.md). The SPEC row `ota` stays
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
| `CONFIRMED` | confirmed by the control plane; the floor not yet advanced | boot, next reset: floor := version → `GOOD` |
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
| The device verifies the CoRIM | The CoRIM is the verifier's artifact (owner, 2026-10-15). The device verifies a SUIT subset carrying the same four facts. |
| An SPM entry point in the AVR boot section so the application can stage an image | It voids the BLB1 mode 3 read barrier from inside: the application could reach boot-section code. The secure-boot architecture forbids it, for the same reason it forbids a CDI trampoline. |
| The W5x00 session in the AVR bootloader | Measured: it does not fit either boot section (ADR 6.1). |
| A serial-attached host doing the network side | Rejected by the owner (2026-10-15). |
| A scratch-page swap for the 2560's A/B | One scratch page is erased once per page swapped. A 64 KB slot is 256 pages, and AVR flash is rated for 10 000 erase cycles, so the scratch page wears out in about forty updates. Swap-move shifts the slot by one page instead and wears every page about twice per update. |
| Advancing the floor on the first boot of a new image | The fallback image would then be below the floor and refused, so the device would have nothing to fall back to. |
| More than one trial, or a device-local confirm | Rejected by the owner (2026-10-15). |
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
