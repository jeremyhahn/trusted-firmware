# ADR: OTA image format, trust and device state

> **Status: PROPOSED (2026-10-15)** · Issue: #52 · Scope: trusted-firmware (image
> format, device side, `sdk/go` upload helper); go-trusted-platform, go-quicbaas
> and go-xkms (seams only) · Last verified: 2026-10-15
>
> Design only. No code, no wire change, no SPEC status change follows from this
> document until the owner accepts it. The open questions at the end need the
> owner's answer first.

## The question

The SPEC row `ota` asks for signed updates of the whole image, A/B slots where a
part has room and the single-slot recovery path where it does not, automatic
fallback when a new image does not confirm, and an anti-rollback floor. Images
come from go-trusted-platform's control plane; each one's manifest is a signed
CoRIM in go-quicbaas's reference-values store. This ADR decides the image format
and its signature, what the boot chain verifies, where the floor's counter lives
on each tier, the slot layout for each arch class, the wire commands OTA would
add, and the device's state machine. It also states what go-trusted-platform and
go-quicbaas must expose, as requirements for those repos.

Shape and data flow: [architecture.md](architecture.md).

## Context

- `tfw-boot` already verifies every boot: it parses a 128-byte trailer at the end
  of the application region, refuses a version below the floor, measures the
  image with SHA-256, compares, and checks the signature over the trailer's
  first 48 bytes ([../secure-boot/api.md](../secure-boot/api.md#the-image-trailer)).
- It already updates: `OTA_BEGIN`/`OTA_DATA`/`OTA_END`/`OTA_STATUS`
  (`0x68`–`0x6B`) over serial, in place, entered by `RESET` mode 1 or by a
  refused boot.
- The bootloader cannot be updated over the wire, by design. The trailer layout
  a deployed `tfw-boot` accepts is therefore fixed for that device's life, and
  `trailer_version` must match exactly.
- On classic AVR only boot-section code can write flash. The application cannot
  stage an image, and a boot-section entry point for it is forbidden.
- The Ethernet link accepts `CAPS`, `APP_INFO` (in an image with an
  application), `ATTEST`, `ATTEST_CERT` and `ATTEST_CSR` only. `tfw-boot` has no
  Ethernet driver.
- `APP_INFO` (`0x06`) reports the linked application's descriptor: its name,
  device profile, `ota_slots` and its version as major, minor and patch. It has
  no `CAPS` bit; the command answering is the capability
  ([../wire-protocol/api.md](../wire-protocol/api.md)).
- The Cortex-M trust-model ADR
  ([../cortex-m/adr-trust-model-vs-tf-m.md](../cortex-m/adr-trust-model-vs-tf-m.md),
  PROPOSED) recommends TF-M with MCUboot as BL2 on the Cortex-M33, our own boot
  chain (option (a)) held in reserve, MPU-only isolation with `tfw-boot` on an
  M7 without an SoC root of trust, and NXP's AHAB verifying the i.MX 95 M7.
- The application SDK declares `ota_slots` (none, single, A/B), `TFW_OTA_SLOTS`
  per board, and `tfw_app_after_update`, whose `TFW_OK` means confirm. Nothing
  calls it yet.
- Flash per AVR part: ATmega328P and ATmega32U4 32 KB with a 4 KB boot section
  (28 KB application region); ATmega2560 256 KB with an 8 KB boot section
  (248 KB region, 64 KB image budget); ATtiny85 8 KB with no boot section.
- Largest payload: `TFW_MAX_PAYLOAD`, 64 bytes.

## Constraints

- Command ids and status codes extend; they never renumber. No wire vector moves.
- No `#if` outside the four places CLAUDE.md names. Slot handling, like every
  other boot backend, is a whole file linked or not.
- The HAL is substituted at link time; slot geometry is boot-HAL data.
- `sdk/go` keeps zero in-house dependencies and holds no private key.
- This project owns no store, registry, rollout or signing key, and reimplements
  nothing another project owns.

## Decisions

### 1. The image format

**One logical manifest, carried per arch class.** Every image carries four
facts under one signature by the image key:

| Fact | Purpose |
|---|---|
| security version | the anti-rollback version; 0 is refused |
| image length | bytes measured, from the slot's base |
| measurement | SHA-256 over those bytes; this is `FWHash` and the CDI input |
| binding | which board and which kind of image it is for |

How they are carried:

- **AVR: trailer version 1, unchanged.** It carries the first three facts. It
  has no binding field, and deployed bootloaders cannot learn one. Binding on
  AVR comes from the key: one image key per board id, pinned at provisioning
  (the pubkey hash in reserved EEPROM, or the element's image-signing slot:
  slot 2 today, slot 15 on the TrustFLEX map the
  [slot-map ADR](../device-provisioning/adr-trustflex-608c-slot-map.md)
  proposes). An image signed for a 328P then fails verification on a 32U4.
  This is open question 5.
- **Cortex-M33 under TF-M (the trust-model ADR's recommendation): MCUboot's
  format.** The image header and its TLVs, signed with `imgtool`. The four facts
  map onto the security counter TLV, the image size, the SHA-256 TLV and the
  signature TLV, with binding by key and image id. No trailer of ours, and no
  second signature. This is open question 12.
- **`tfw-boot` ports (Cortex-M33 under option (a), Cortex-M7 without an SoC root
  of trust): trailer version 2.** The same 128 bytes and
  offsets. The two reserved bytes at offsets 6 and 7, which are inside the signed
  span and zero in version 1, become `board_id` and `image_flags`. The signed
  span stays bytes `[0, 48)`. A version 1 parser refuses a version 2 trailer and
  the other way round, as the exact match already requires. The trailer sits at
  the end of the slot. The measurement covers the image from the slot base, so
  it is the same in either slot. The signature is Ed25519, or ECDSA P-256 as
  `r‖s` where the key tier is an element; 64 bytes either way.
- **Composed vendor boot (ESP32-P4): the vendor's signed format.** The four
  facts map onto ESP-IDF's app descriptor `secure_version`, its appended SHA-256
  and its secure boot v2 signature block. No second signature of ours. The
  port's ADR states the mapping. This is open question 6. The RP2350 was drawn
  here on its boot ROM's format; the trust-model ADR counts it among the TF-M
  ports, so it follows the Cortex-M33 bullet above (open question 12).

`image_flags` bit 0 marks a development image, which a production key never
signs; the other bits are reserved and zero.

### 2. What the boot chain verifies

- **Unchanged order, every boot, every slot:** trailer parse, floor, measurement,
  signature. On A/B this includes the fallback slot before the device falls back
  to it. A version 2 trailer adds one check after the parse: `board_id` equals
  the board's own.
- **The bootloader is the authority.** On A/B parts the application checks a
  staged image before it can be activated. That check only saves a reboot into
  a refusal; nothing the application decided is trusted at boot.
- **The device does not verify the CoRIM.** It verifies the signature and the
  measurement against the trailer. The CoRIM carries the same digest. The
  control plane checks the image against it before sending, and the verifier
  appraises the device's reported `FWHash` against it afterwards. This is how
  this ADR reads the SPEC row's "accepted only if its signature verifies and its
  measurement matches that manifest"; open question 3.

### 3. Anti-rollback

- **One counter, the security version.** The trailer's `image_version` is the
  security version, as shipped. The version for people is the application
  descriptor's major, minor and patch, which `APP_INFO` already reports, and
  the CoRIM carries it for the store. Neither is in the trailer, and boot
  compares neither.
- **The floor advances on confirm on A/B parts, and on the first verified boot
  on single-slot parts.** On A/B, advancing at the trial boot would put the
  fallback image below the floor. On a single slot there is nothing to fall
  back to, so the shipped behaviour stays.
- **`TFW_BOOT_FLOOR_MAX_STEP` (16) stays** on every counter that can be
  exhausted.
- **The counter, per tier:**

| Tier / part | Counter | Holds against |
|---|---|---|
| none (ATtiny85) | none: no secure boot | nothing |
| AVR soft | `rollback_floor` in reserved EEPROM (shipped) | an old signed image replayed over the wire; **not** code running on the device or ISP, since any AVR code can write EEPROM |
| AVR element (ATECC608C) | the element's monotonic counter 0, `TFW_ATECC_COUNTER_BOOT` (shipped; the slot-map ADR keeps it) | code execution in the application; about 2 000 000 increments for the part's life |
| SE052F, OPTIGA Trust M (future rows) | the element's monotonic counter object | as the ATECC608C |
| ESP32-P4 | the eFuse secure version, through ESP-IDF's anti-rollback | one-way bits; a small fixed number of steps for the part's life |
| Cortex-M33 under TF-M (STM32H5, nRF54L15, MCXN947, RP2350) | MCUboot's per-image security counter, in TF-M's non-volatile counters | code in the non-secure world |
| Cortex-M33 under option (a), Cortex-M7 without an SoC root of trust | a counter in OTP or secure flash, chosen in each port's ADR | code in the non-secure world, or outside the MPU region on the M7 |
| i.MX 95 M7 | the AHAB container version or an enclave counter, never writable from the A55 | open question 11 |

### 4. Slot layout per arch class

- **ATtiny85: none.** No boot section, so no secure boot and no update path.
  `TFW_OTA_SLOTS` is 0 in effect: an application declaring
  `TFW_APP_OTA_SINGLE` refuses to start there, as the application SDK already
  checks.
- **ATmega328P and ATmega32U4: single slot, in place (shipped).** Two 28 KB
  slots need 56 KB of a 32 KB part.
- **ATmega2560: single slot by default; A/B possible.** The 248 KB region holds
  two slots for the 64 KB image budget. If the owner chooses it (open
  question 2), the design is **swap-move**: the primary slot is one page larger
  than the secondary; the bootloader shifts the primary up one page, then swaps
  page by page, recording progress in reserved EEPROM so a power cut resumes the
  swap. Images stay linked for address 0. The boot HAL must read above 64 KB
  (far reads) to reach the secondary slot. The flash cost in the 8 KB boot
  section is unmeasured. Moving a Mega to A/B changes its bootloader, so it is
  a bench reflash, never an update.
- **Cortex-M33 under TF-M: MCUboot's primary and secondary slots.** The
  non-secure application writes the secondary slot through PSA Firmware Update,
  and MCUboot's own test and confirm give the trial and the fallback. Composed,
  never reimplemented.
- **Cortex-M `tfw-boot` ports: A/B.** Hardware bank swap where the part has it
  (the STM32H5's dual bank); swap-move otherwise. The geometry is boot-HAL data.
- **Composed vendor boot: the vendor's A/B.** ESP-IDF's `ota_0`/`ota_1` and
  `otadata`, and its app rollback. Composed, never reimplemented.

### 5. Wire commands

**AVR adds none.** The four shipped commands, served by the bootloader over
serial, are the whole AVR surface.

**A/B parts**, proposed ids only, in the OTA block, extending:

| Id | Command | Request | Response | Served by |
|---|---|---|---|---|
| `0x68`–`0x6B` | `OTA_BEGIN`, `OTA_DATA`, `OTA_END`, `OTA_STATUS` | unchanged | unchanged | the application as well, writing the inactive slot |
| `0x6C` | `OTA_INFO` | `[slot:1]` | `[slots:1][active:1][flags:1][floor:4][state:1][image_version:4][measurement:32]` (44 bytes) | the application |
| `0x6D` | `OTA_ACTIVATE` | `[mode:1]`: 0 at the next reset, 1 reset now | none | the application |
| `0x6E` | `OTA_CONFIRM` | none | none | the application, in `TRIAL` only |

- `0x6F` stays unallocated.
- `OTA_END` keeps its meaning: every byte arrived, nothing verified.
- `OTA_INFO`'s `state` is the slot state of [architecture.md](architecture.md#ab).
  `flags` bit 0 says the last boot fell back. It is read-only.
- `OTA_ACTIVATE` and `OTA_CONFIRM` need the operator auth session on the element
  tier, as `RESET` does, and answer `TFW_ERR_AUTH_REQUIRED` (17) without it.
- **One new status, proposed:** `TFW_ERR_BAD_STATE` = 19, "the command is not
  valid in the current OTA state": activate with no `VERIFIED` slot, confirm
  outside `TRIAL`, begin while a slot is `PENDING`. An older host reads 19 as
  unrecognised. Reusing an existing code is open question 10.
- A host tells an A/B device from a single-slot one by `OTA_INFO` answering. An
  image without it answers `TFW_ERR_UNKNOWN_CMD`, as for any unknown id. No
  `CAPS` bit, following `APP_INFO`.
- On an M33 under TF-M the application maps these commands onto PSA Firmware
  Update calls; the wire is the same.
- On A/B images that carry Ethernet, the allow-list (`0x00`, `0x06`, `0x60`,
  `0x63`, `0x64` today) gains `0x68`–`0x6E`. On AVR it gains nothing, because
  the application does not serve them.

### 6. The device state machine

As drawn in [architecture.md](architecture.md#device-state-machine). The
decisions in it:

- The trial is bounded by an attempt count written before each jump, and a hung
  trial by the watchdog. Default: one attempt.
- **Confirm is local by default**: `init` returned `TFW_OK` and
  `tfw_app_after_update` returned `TFW_OK`. A policy set by the control plane can
  hold the confirm for its own `OTA_CONFIRM`, sent after the new image's
  attestation is appraised. Which is the default is open question 7.
- A fallback is reported by `OTA_INFO`'s flag and, in the evidence, by the old
  `FWHash`: the control plane sees the image it did not send.

### 7. The `sdk/go` upload helper

Proposed names, not implemented. Standard library only; no private key; no
signature verification, since that is the device's.

| Name | Does |
|---|---|
| `tfw.ParseImage(b []byte) (*Image, error)` | checks structure only: whole pages, trailer magic and version, length, and `board_id` on version 2 |
| `(*Device).Upload(ctx, *Image, ...UploadOption) error` | `OTA_BEGIN`, stop-and-wait `OTA_DATA` in chunks that fit `TFW_MAX_PAYLOAD`, `OTA_END`; resumes inside a session from `OTA_STATUS`'s received count; on single-slot parts waits for the evaluation `EVENT` |
| `(*Device).OTAInfo(ctx, slot byte) (*SlotInfo, error)` | `OTA_INFO` |
| `(*Device).Activate(ctx, now bool) error` | `OTA_ACTIVATE` |
| `(*Device).Confirm(ctx) error` | `OTA_CONFIRM` |

`tfwctl` gains an `ota` verb over the same calls. go-trusted-platform calls the
helper on the device's connection; it never encodes an `OTA_*` frame itself.

## Seams: what the other repos must expose

These are requirements stated for those repos, in their terms. Filing them is
those repos' work; this ADR only reports them.

### go-trusted-platform

> **device-ota** — The control plane delivers firmware images to
> trusted-firmware devices through its device-facing endpoint, on the
> connection the device opens. It owns fleet policy and rollout: which image a
> device gets, keyed by board id, application and device profile; the waves;
> pausing a rollout on a failure rate; and the confirm policy (device-local, or
> held for the control plane's confirm after the new image's attestation is
> appraised). It learns a device's application, profile and version from
> `APP_INFO` (`0x06`), through `sdk/go`. Before sending an image it fetches the image and its CoRIM from
> go-quicbaas's reference-values store and refuses one whose digest does not
> match. It drives the transfer through `sdk/go`'s upload helper and never
> encodes the frames itself. After an update it attests the device and
> appraises the new `FWHash` through go-quicbaas; a device reporting a
> measurement outside the store, or the old one after a fallback, is recorded
> and the rollout acts on it. trusted-firmware's SPEC (`ota`) names the same
> seam.

This widens requirement R1 of the
[onboarding architecture](../device-onboarding/architecture.md#requirements-on-other-repositories),
which today has the endpoint issue only `CAPS` and the three attestation
commands; see open question 8.

### go-quicbaas

> **firmware-reference-values** — The reference-values store holds, for each
> trusted-firmware image, the image bytes and a signed CoRIM whose reference
> value is the image's SHA-256 measurement, keyed by board id, application and
> security version. An entry is immutable once written. The store refuses an
> image whose digest differs from its CoRIM's. It serves the image and the CoRIM
> to go-trusted-platform, and the CoRIM to the verifier that appraises a
> device's `FWHash`. trusted-firmware keeps no copy and no store of its own.
> trusted-firmware's SPEC (`ota`) names the same seam.

### go-xkms (reported, not this brief's seam)

Appraising a DICE `FWHash` against a CoRIM reference value, and encoding and
signing CoRIM, are go-xkms's under the ownership rules. If either is missing it
is a gap there. The custody of the image-signing key is open question 4.

## Rejected alternatives

| Alternative | Rejected because |
|---|---|
| A new trailer for AVR with a board field | A deployed `tfw-boot` cannot learn it: the bootloader is never updated over the wire, and a version mismatch is refused. Every AVR in the field would reject every new image. |
| The device verifies the CoRIM | It does not fit a 4 KB boot section, and it makes the device trust a second signer for the digest the trailer already binds. |
| Two signatures (ours and the vendor's) on composed platforms | Two keys, two custody stories and two refusal paths for one fact. The vendor's chain is what the part's ROM enforces anyway. |
| Separate security and product versions in the trailer | A second counter field the bootloader ignores is an invitation to compare the wrong one. The product version is metadata: `APP_INFO` reports it from the descriptor and the CoRIM carries it. |
| An SPM entry point so the AVR application can stage an image | It voids the boot section's read barrier from inside. |
| A scratch-page swap on the ATmega2560 | The scratch page wears out in about twenty updates; see [architecture.md](architecture.md#alternatives-rejected). |
| `OTA_*` on the AVR Ethernet allow-list | The application does not serve them, and the bootloader does not speak Ethernet. Listing them would answer the network with a command the image cannot run. |
| A new status for every refusal (no slot, wrong state, trial over) | One "bad state" code says what a host can act on. Each extra code is one more an older host must read as unrecognised. |

## Open questions for the owner

1. **Network OTA for AVR.** An AVR device on Ethernet alone cannot be updated
   over the network: the bootloader is serial-only and the application cannot
   write flash. Options: (a) a serial-attached host near the device runs the
   upload helper for the control plane; (b) a W5x00 driver in the ATmega2560's
   8 KB boot section, so its bootloader takes the session over TCP (the
   ATmega328P's 4 KB section cannot hold it); (c) AVR is updated on the bench
   only. Recommendation: (a) now, and measure (b).
2. **A/B on the ATmega2560**, by swap-move, or single slot like the 328P. It
   costs boot-section flash not yet measured, and a bench reflash of every Mega.
3. **Where "accepted" happens.** This ADR has the device check signature,
   measurement and floor; the control plane check the CoRIM before sending; and
   the verifier appraise `FWHash` after. Is that the SPEC row's meaning, or must
   devices with room also verify the CoRIM?
4. **Custody of the image key and the CoRIM signer.** Proposed: both in go-xkms,
   reached through go-quicbaas, with this repo supplying only the trailer's
   to-be-signed bytes and the serialiser. No private key in this repo or in
   `sdk/go`. How the public key reaches the element's image-signing slot is
   also open; the slot-map ADR records that no document says.
5. **Board binding on AVR** by one image key per board id, or accept that a
   signed image for one AVR board verifies on another with the same key. Per
   board id, each element's image-signing slot holds its board's key.
6. **Composed platforms**: the vendor's signature only, or the vendor's plus our
   trailer.
7. **Confirm default**: device-local, or held for the control plane's confirm
   after attestation.
8. **Activation over Ethernet.** `OTA_ACTIVATE` and `OTA_CONFIRM` need the
   operator auth session, which is serial-only today. Allow the auth session on
   Ethernet, or let the signed image and the floor stand alone there. Either
   way the onboarding architecture's R1, which limits the endpoint to `CAPS`
   and the attestation commands, must widen to the `OTA_*` ids.
9. **Trial attempts**: one, or more for parts whose first boot is slow to
   settle.
10. **Status 19** as a new code, or reuse an existing one (`TFW_ERR_BUSY`, 8,
    reads closest but means a peripheral).
11. **i.MX 95 M7.** The trust-model ADR has AHAB verify the M7 image inside
    trusted-imx95's container, with the floor in the AHAB container version or
    an enclave counter, never writable from the A55. Who stages the image is
    that ADR's open question 7. This ADR adds no command for the M7 until that
    is answered.
12. **The Cortex-M33 update path.** The trust-model ADR recommends TF-M with
    MCUboot and PSA Firmware Update (its option (b1)), so this ADR uses
    MCUboot's format and slots there, and trailer version 2 only under option
    (a) and on an M7 without an SoC root of trust. Three points need the
    owner's answer with that ADR's open questions 1 (which model on the
    NUCLEO-H563ZI) and 6 (where the CoRIM check runs under MCUboot, which is
    also question 3 here):
    - whether the RP2350 follows TF-M and MCUboot, as that ADR lists it, or
      its boot ROM's own A/B and OTP rollback version, as this ADR first drew
      it;
    - whether the non-secure application serving `0x68`–`0x6E` over PSA
      Firmware Update is the device side of go-trusted-platform's OTA endpoint that ADR names;
    - whether `tfw.ParseImage` should also read MCUboot's header, or leave it
      to `imgtool`.

## Follow-ups once accepted

- The SPEC row `ota`'s Detail cell names `docs/ota/`; its status stays the
  owner's to move.
- `engineering.md`, `api.md` and `user.md` in this folder, with the code.
- The boot-section measurement for options 1(b) and 2 on the ATmega2560.
- go-trusted-platform's `device-ota` and go-quicbaas's
  `firmware-reference-values` rows, filed by those repos.
