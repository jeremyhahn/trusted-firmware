# OTA — architecture

> **Status: PROPOSED (2026-10-15)** · Issue: #52 · Scope: trusted-firmware (image
> format, device side, `sdk/go` upload helper); go-trusted-platform and
> go-quicbaas (seams only) · Last verified: 2026-10-15
>
> Becomes NORMATIVE when [adr-ota-image-format-and-trust.md](adr-ota-image-format-and-trust.md)
> is accepted. No code exists for anything here that is not already shipped by
> [`secure-boot/`](../secure-boot/architecture.md). The SPEC row `ota` stays
> `future`.

OTA replaces the whole image, firmware and application together, with a signed
one, and never lowers its version. On a part with room for two images, a new
image runs on trial and the device falls back to the old one by itself if the
new one does not confirm. On a part without room, the update is written in
place by the bootloader, which is the single-slot path `tfw-boot` ships today.

Decisions and their reasons are in the ADR. This file holds the shape:
who owns what, how an image travels, what the device does with it.

## Ownership

| Piece | Owner | This project's part |
|---|---|---|
| Image format: the trailer, its signed span, the measurement | trusted-firmware | owns it |
| Verifying an image before it runs, the anti-rollback floor, slots, trial and confirm | trusted-firmware (`tfw-boot` and its ports), or MCUboot or the vendor boot ROM where a port composes one | owns it, or composes theirs |
| The upload helper: driving `OTA_*` on a device connection | trusted-firmware (`sdk/go`) | owns it |
| Which device gets which image, when, in which wave; pausing a rollout; the confirm policy | go-trusted-platform (fleet policy, rollout) | none: states the seam |
| The device-facing endpoint the device connects to | go-trusted-platform | none: states the seam |
| Image bytes and their signed CoRIM manifest | go-quicbaas (reference-values store) | none: states the seam |
| CoRIM encoding and signing, appraisal of the post-update `FWHash` | go-xkms (through go-quicbaas) | none: reports the need |
| The private keys that sign images and manifests | not this project; see the ADR's open questions | holds none |

This project keeps no registry, store, update server or private key. A gap in
any of the other repos is reported against that repo, never worked around
here.

## Data flow

```
build (reproducible)  ──► image + trailer to-be-signed bytes
                              │  signed by the image key (custody: open question)
                              ▼
                       signed image ──► CoRIM manifest (digest = trailer measurement)
                              │                │
                              └──► go-quicbaas reference-values store ◄──┘
                                              │
            go-trusted-platform rollout ──────┤ picks image for (board, application, device),
                                              │ the application as APP_INFO reports it
                                              │ checks the image digest against its CoRIM
                                              ▼
device ──(opens the connection)──► go-trusted-platform device-facing endpoint
                                              │ sdk/go upload helper, as the host:
                                              │ OTA_BEGIN, OTA_DATA..., OTA_END
                                              ▼
device: verify (signature, measurement, floor) ──► activate ──► trial boot
        ──► confirm (or automatic fallback) ──► floor advances ──► ATTEST
                                              │
            go-trusted-platform ◄─────────────┘ new FWHash, appraised against the CoRIM
```

- The device opens the connection; it never listens. That is the Ethernet
  transport's rule and OTA does not change it.
- The host side of every `OTA_*` exchange is `sdk/go`, imported by
  go-trusted-platform. On a bench it is `tfwctl` on a serial port.
- The device trusts only the signature and the floor. The CoRIM is checked by
  the control plane before it sends an image and by the verifier after the
  device reports the new `FWHash`. A device running an image outside the store
  fails appraisal at its next attestation.

## Arch classes

| Class | Parts | Layout | Who verifies | Floor |
|---|---|---|---|---|
| AVR, no boot section | ATtiny85 | none: no secure boot, no OTA | nobody | none |
| AVR, 4 KB boot section | ATmega328P, ATmega32U4 | single slot, in place, written by `tfw-boot` (shipped) | `tfw-boot` | reserved EEPROM (soft tier) or the element's monotonic counter |
| AVR, 8 KB boot section | ATmega2560 | single slot today; A/B by swap-move is an option (open question) | `tfw-boot` | as above |
| Cortex-M33 under TF-M ([trust-model ADR](../cortex-m/adr-trust-model-vs-tf-m.md) (b1)) | STM32H5, nRF54L15, MCXN947, RP2350 | MCUboot's primary and secondary slots, written through PSA Firmware Update | MCUboot (BL2) | MCUboot's per-image security counter |
| Cortex-M, `tfw-boot` port | an M33 under option (a); an M7 without an SoC root of trust | A/B: hardware bank swap where the part has it, swap-move otherwise | `tfw-boot` port | a counter in OTP or secure flash, per port ADR |
| Composed vendor boot | ESP32-P4 (ESP-IDF) | the vendor's A/B partitions | the vendor chain | the vendor's one-way fuses |
| Platform image | i.MX 95 M7 | trusted-imx95's container; who stages it is open | AHAB | the AHAB container version or an enclave counter, never writable from the A55 |

**What AVR gets.** The ATmega328P and ATmega32U4 get exactly what ships: the
bootloader's four-command session over serial, entered by `RESET` mode 1 or by
a failed boot, writing in place. No new wire command, no new state, no A/B. An
interrupted update leaves the device in recovery until a good image arrives.
The ATtiny85 gets nothing: it has no boot section, so no secure boot and no
update path. The ATmega2560 gets the same as the 328P unless the owner chooses
A/B for it.

**AVR cannot stage an image from the application.** On classic AVR only code in
the boot section can execute `SPM`, so the application cannot write flash. A
boot-section entry point the application could call would void the BLB1 read
barrier that keeps the UDS out of its reach. So an AVR image always travels
through the bootloader, and the bootloader speaks serial only. An AVR device
on Ethernet alone cannot be updated over the network today. That is an open
question for the owner, with options, in the ADR.

## Device state machine

### Single slot (AVR, shipped)

```
RUNNING ──RESET mode 1──► SESSION (bounded) ──timeout, nothing written──► RUNNING
   ▲                          │
   │                          ├─OTA_BEGIN, OTA_DATA (first page erased: old image gone)
   │                          ▼
   │                      RECEIVING ──timeout or failure──► RECOVERY (unbounded)
   │                          │                                │
   │                          └─OTA_END ─► evaluate ◄──────────┘ (OTA_END)
   │                                          │
   └──── floor advances, jump ◄── TFW_OK ─────┴── refused ──► RECOVERY
```

The floor advances on the first verified boot of the new image, because there
is no older image to fall back to. `tfw_app_after_update` is called on that
boot; a refusal is reported, as the application SDK states, and the image
stays.

### A/B

Each slot has a state. The bootloader owns the transitions marked `boot`; the
application owns the rest.

| State | Meaning | Left by |
|---|---|---|
| `EMPTY` | no image | `OTA_BEGIN` (app) → `RECEIVING` |
| `RECEIVING` | bytes arriving into the inactive slot | `OTA_END` with every byte → `STAGED`; a new `OTA_BEGIN` restarts |
| `STAGED` | every byte arrived; nothing verified | the advisory check → `VERIFIED` or `FAILED` |
| `VERIFIED` | trailer, floor, measurement and signature passed the advisory check | `OTA_ACTIVATE` → `PENDING` |
| `PENDING` | to be tried on the next boot | boot: full evaluation → `TRIAL`, or → `FAILED` |
| `TRIAL` | running, not confirmed; boot attempts left counted down before each jump | confirm → `CONFIRMED`; boot: attempts exhausted → `FAILED` and fall back |
| `CONFIRMED` | the image the device boots | replaced when another slot confirms → `FALLBACK` |
| `FALLBACK` | the previous confirmed image, kept for a fallback | overwritten by the next `OTA_BEGIN` |
| `FAILED` | refused or did not confirm | overwritten by the next `OTA_BEGIN` |

```
inactive slot:  EMPTY/FAILED/FALLBACK ─OTA_BEGIN─► RECEIVING ─OTA_END─► STAGED
                  ─check─► VERIFIED ─OTA_ACTIVATE─► PENDING ─reset─►
boot:           evaluate PENDING slot ─ok─► TRIAL (attempts--) ─jump─►
application:    init OK, after_update OK [, host OTA_CONFIRM] ─► CONFIRMED
                                                                 floor := version
                                                                 old slot := FALLBACK
boot, TRIAL with no attempts left:  slot := FAILED, boot FALLBACK (evaluated as always)
```

- The bootloader evaluates the slot it is about to boot, on every boot, with
  the shipped order: trailer, floor, measurement, signature. That includes
  the fallback slot. The application's check before `VERIFIED` saves a reboot
  into a refusal; it is not a security boundary.
- The floor advances on **confirm**, not on the first boot. Advancing it on the
  trial boot would make the fallback image older than the floor, and the
  fallback would be refused.
- A trial that hangs is ended by the watchdog. The attempt counter is written
  before the jump, so a crash on every boot still runs out.

## Trust boundaries

- **The signature and the floor are the only things the device trusts.** The
  transport, the control plane and the store are not trusted for integrity:
  any byte of the image can be wrong and the device refuses it. The control
  plane is trusted only to choose which signed image a device should run.
- **The bootloader is the authority.** Nothing the application decided about an
  image survives into the boot decision except the slot states, and every
  slot is evaluated again before it runs.
- **The bootloader is never updated over the wire.** The trailer format a
  deployed `tfw-boot` accepts is fixed for that device's life. A change to the
  trailer reaches deployed AVR devices only by a bench reflash.
- **Anti-rollback is as strong as its counter.** The software tier's EEPROM
  floor stops a replayed old image arriving over the wire. It does not stop an
  attacker who runs code on the device, since any AVR code can write EEPROM.
  The element tier's counter does.
- **Activation changes what runs.** On A/B parts `OTA_ACTIVATE` and
  `OTA_CONFIRM` need the operator auth session on the element tier, as
  `RESET` already does.

## Alternatives rejected

| Alternative | Rejected because |
|---|---|
| The device verifies the CoRIM | CBOR, COSE and a second key do not fit a 4 KB boot section, and the device would trust two signers for one fact. The trailer already binds version and measurement under the image key; the CoRIM is the fleet's record of the same digest. |
| An SPM entry point in the AVR boot section so the application can stage an image | It voids the BLB1 mode 3 read barrier from inside: the application could reach boot-section code. The secure-boot architecture forbids it for the same reason it forbids a CDI trampoline. |
| A scratch-page swap for an AVR A/B | One scratch page is erased once per page swapped. A 120 KB slot is 480 pages, and AVR flash is rated for 10 000 erase cycles, so the scratch page wears out in about twenty updates. Swap-move shifts the slot by one page instead and wears every page about twice per update. |
| Advancing the floor on the first boot of a new A/B image | The fallback image would then be below the floor and refused, so the device would have nothing to fall back to. |
| A pull model: the device downloads from a URL | The device has no HTTP client and no TLS, and the device never chooses its own image: that is fleet policy, which is go-trusted-platform's. |
| A store, registry or update server in this repo | go-quicbaas owns the store and go-trusted-platform the endpoint and rollout; a second one here is the duplication L1 forbids. |

## What this deliberately does not do

- **No delta updates.** A delta needs the old image readable at the destination
  and a patcher in the trusted computing base. Whole images only.
- **No encryption of the image.** Confidentiality of firmware is the platform's
  flash-encryption feature where a part has one (ESP32), not this format's.
- **No bootloader update over the wire**, on any class.
- **No partial updates of the application alone.** The image is one
  measurement, firmware and application together.
- **No rollout logic on the device.** The device does not decide when to
  update; it accepts or refuses what it is sent.
