# ADR: OTA image format, trust and device state

> **Status: ACCEPTED (owner, 2026-10-15) for the decisions marked ACCEPTED;
> the parts marked PROPOSED still need a choice** · Issue: #52 · Scope:
> trusted-firmware (image format, device side, `sdk/go` upload helper);
> go-trusted-platform, go-quicbaas, go-xkms and qrdb (seams only) · Last
> verified: 2026-10-15
>
> The owner answered the twelve questions of the first draft on 2026-10-15
> (f518455), corrected answers 1 and 6 at 16:15Z and settled answer 9 at
> 16:25Z. Each decision cites that ruling as "owner, 2026-10-15, answer N".
> Design and measurement only. No code, wire change or SPEC status change
> follows from this document; the code is the follow-ups at the end.

## The question

The SPEC row `ota` asks for signed updates of the whole image, A/B slots where a
part has room, automatic fallback when a new image does not confirm, and an
anti-rollback floor. Images come from go-trusted-platform's control plane; the
fleet's record of each image is a signed CoRIM in go-quicbaas's reference-values
store. This ADR decides:

- the image format and its signature;
- the manifest the device verifies;
- what the boot chain verifies, and where the floor's counter lives on each tier;
- how each AVR part receives an image over the network and over serial, with
  measured flash;
- the slot layout per arch class;
- the wire commands OTA adds;
- the device's state machine;
- what go-trusted-platform, go-quicbaas, go-xkms and qrdb must expose, as
  requirements on those repos.

Shape and data flow: [architecture.md](architecture.md).

## Context

- `tfw-boot` verifies every boot. It parses a 128-byte trailer at the end of the
  application region, refuses a version below the floor, measures the image
  with SHA-256, compares, and checks the signature over the trailer's first 48
  bytes ([../secure-boot/api.md](../secure-boot/api.md#the-image-trailer)).
- The boot build links one backend per concern: `BOOT_TIER`, `BOOT_MEASURE`,
  `BOOT_SIG` and `BOOT_OTA`. With `BOOT_OTA=1` it serves
  `OTA_BEGIN`/`OTA_DATA`/`OTA_END`/`OTA_STATUS` (`0x68`–`0x6B`) over serial,
  writing in place, entered by `RESET` mode 1 or by a refused boot. With
  `BOOT_OTA=0` a refused boot halts.
- **What ships.** Every AVR board ships the element tier in boot (ATECC608C
  measure and verify). The ATmega328P and ATmega32U4 ship `BOOT_OTA=0`: they
  have **no update path at all** today; a refused image needs a programmer. The
  first draft said they had a single-slot path; that was wrong. Only the
  ATmega2560 can carry `BOOT_OTA=1`.
- The bootloader is never updated over the wire. Any change to it reaches a
  deployed board by a bench reflash, which the owner accepts for every change
  below (owner, 2026-10-15, answers 2 and 6).
- On classic AVR only boot-section code can execute `SPM`, so the application
  cannot write flash. A boot-section entry point for it is forbidden: it voids
  the BLB1 read barrier that keeps the UDS out of the application's reach.
- The Ethernet link (W5100 or W5500 on SPI, `ETH=1`) accepts `CAPS` (`0x00`),
  `APP_INFO` (`0x06`), `ATTEST` (`0x60`), `ATTEST_CERT` (`0x63`) and
  `ATTEST_CSR` (`0x64`). `ETH=1` links the TCP link *in place of* the
  serial-only one; the TCP link keeps the UART, so an `ETH=1` image speaks both.
  `tfw-boot` has no Ethernet driver.
- The application SDK declares `ota_slots` (none, single, A/B), `TFW_OTA_SLOTS`
  per board, and `tfw_app_after_update`. Nothing calls it yet.
- Flash per AVR part:

| Part | Flash | Boot section | Application region | `TFW_FLASH_BUDGET` |
|---|---|---|---|---|
| ATmega328P | 32 768 | 4 096 | 28 672 | 16 384 |
| ATmega32U4 | 32 768 | 4 096 | 28 672 | 14 336 |
| ATmega2560 | 262 144 | 8 192 | 253 952 | 65 536 |
| ATtiny85 | 8 192 | none | 8 192 | 6 144 |

- Largest payload: `TFW_MAX_PAYLOAD`, 64 bytes.

## Constraints

- Command ids and status codes extend; they never renumber. No wire vector moves.
- No `#if` outside the four places CLAUDE.md names. Transports, slot handling
  and the external store are whole files linked or not.
- The HAL is substituted at link time; slot and store geometry are boot-HAL and
  board data.
- `sdk/go` keeps zero in-house dependencies and holds no private key.
- Frameworks first: a manufacturer's or community's update framework is used
  where it does the job, and our code is written only where it fails or for the
  common abstraction (owner, 2026-10-15, answer 12).
- This project owns no store, registry, rollout or signing key, and reimplements
  nothing another project owns.

## Decisions

### 1. The four facts — ACCEPTED

Owner, 2026-10-15, answers 3 and 6. Every image carries four facts under the
image key's signature, in two forms on the device (the trailer and, where it
fits, the manifest of decision 3) and in a third form for the fleet (the CoRIM):

| Fact | Trailer v1 | Manifest (decision 3) | CoRIM (go-quicbaas, the verifier's) |
|---|---|---|---|
| security version | `image_version` @8 | `suit-manifest-sequence-number` | the reference value's security version |
| length | `image_len` @12 | `suit-parameter-image-size` | the measured length |
| measurement | SHA-256 @16 | `suit-parameter-image-digest` | the reference value's digest |
| binding | `board_id` @6, `image_flags` @7 | `suit-parameter-class-identifier` | the environment's class |

A device that verifies both forms checks that they agree, field by field. A
mismatch is a refusal. The CoRIM is never verified on the device; it stays the
verifier's artifact (owner, 2026-10-15, answer 3).

How the facts are carried per arch class:

- **AVR: trailer version 1 with the board id** (decision 2).
- **Cortex-M33 under TF-M: MCUboot's format.** The header and TLVs, signed with
  `imgtool`: security counter TLV, image size, SHA-256 TLV, signature TLV, and
  binding by key and image id. Pending the trust-model ADR (open question 9).
- **`tfw-boot` ports (an M33 under the trust-model ADR's option (a), an M7
  without an SoC root of trust): trailer version 1 with the board id**, at the
  end of the slot. The measurement covers the image from the slot base, so it is
  the same in either slot.
- **Composed vendor boot (ESP32-P4): the vendor's signature plus our trailer**
  (owner, 2026-10-15, answer 6; decision 11).
- **i.MX 95 M7: AHAB's container** (decision 12, pending #15).

### 2. Trailer v1 with the board id — ACCEPTED

Owner, 2026-10-15, answer 6, corrected at 16:15Z: there is no trailer version 2.

- The trailer stays **version 1**, the same 128 bytes and offsets. The two
  reserved bytes become **`board_id` at byte 6** and **`image_flags` at byte
  7**. Both lie inside the signed span `[0, 48)`, which does not change.
- `board_id` is the board-id registry's value: ATmega328P `0x01`, ATmega2560
  `0x02`, ATmega32U4 `0x03`, ATtiny85 `0x04`, the Cortex-M range `0x40`–`0x7F`,
  esp32p4-poe `0x80`, esp32-qemu `0x81`.
- `image_flags` bit 0 marks a development image, which a production key never
  signs. The other bits are reserved and zero.
- **Today's parser** (`tfw_boot_trailer_parse`) never reads bytes 6 and 7. It
  accepts any value there, and the signature authenticates them because they are
  in the signed span. A deployed bootloader therefore boots a new-format image,
  but it does not check the binding.
- **The new `tfw-boot` checks** `board_id` against its own `TFW_BOARD_ID`,
  after the parse and before the floor. An image for another board is refused
  as any refused image is: no state change, nothing written. A `board_id` of 0,
  which today's images carry, is refused too, so every image is rebuilt with its
  board's id.
- Every bench board is reflashed with the new `tfw-boot`, and every image is
  rebuilt; no backward compatibility is kept (owner, 2026-10-15, answers 2 and
  6).
- One image key per board id (decision 10) binds the same fact a second way: an
  image signed for a 328P fails verification on a 2560 even before the byte is
  read.

### 3. The manifest — ACCEPTED (format), PROPOSED (carriage and key sharing)

Owner, 2026-10-15, answer 3: the device verifies a signed compact manifest
beside the image, in the SUIT family, on every board with room; the 328P
verifies at least the signed trailer.

- **Profile: a subset of the SUIT manifest** (draft-ietf-suit-manifest, in the
  RFC Editor queue, with its IANA registry). It is the authentication half of the
  constrained MTI profile `suit-sha256-es256-ecdh-a128ctr` of
  draft-ietf-suit-mti, with no encryption and no key exchange, since image
  encryption is a non-goal ([architecture.md](architecture.md#what-this-deliberately-does-not-do)).
  - **Algorithms:** digest SHA-256 (COSE `-16`); signature ES256 (COSE `-7`,
    ECDSA P-256) in a `COSE_Sign1` (tag 18).
  - **Envelope:** tag 107, with the authentication wrapper (the manifest's
    digest and one `COSE_Sign1`) and the manifest.
  - **Manifest:** version 1; the sequence number (the security version); one
    component; the common shared sequence's override-parameters, carrying the
    vendor id, the class id (binding), the image digest and the image size.
  - **No** directives beyond the common sequence, no dependencies, no
    severable members, no text. A device refuses any member it does not know.
- **ES256 everywhere.** The ATECC608C verifies only P-256, and software Ed25519
  fits no AVR boot section (decision 5). One algorithm keeps one key type per
  board in go-xkms. EdDSA was weighed and rejected (see Rejected alternatives).
- **Who verifies what:**

| Board | Manifest | Trailer | Where |
|---|---|---|---|
| ATmega2560 | yes | yes | manifest in the application, before activation; trailer in `tfw-boot`, every boot |
| ATmega328P, ATmega32U4 | **only under the region budget** (decision 6) | yes | as the 2560; under the 16 384 policy line the trailer alone |
| ATtiny85 | no | no | no secure boot (decision 6) |
| ESP32-P4 | yes | yes, beside the vendor's signature | decision 11 |
| Cortex-M33 under TF-M | proposed: in the non-secure application before PSA FWU install | MCUboot's TLVs | open question 9 |
| i.MX 95 M7 | none until #15 | AHAB | decision 12 |

- **The bootloader is the authority; the manifest check is a gate before
  activation.** On AVR the manifest reader is about 2 900 bytes (measured,
  decision 6), which fits no boot section beside the rest. So the application
  verifies the manifest and refuses to activate an image whose manifest fails,
  and `tfw-boot` verifies the trailer on every boot. The manifest adds the
  binding by class id and an independently encoded copy of the four facts. It
  does not move the trust boundary.
- **The encoder and the signer.** This project's image build emits the
  manifest's to-be-signed bytes, as it emits the trailer's. go-xkms signs both,
  the manifest as `COSE_Sign1` (decision 10). No COSE signing code lives here.
- **PROPOSED — carriage.** The signed envelope travels in the same `OTA_DATA`
  stream, after the image: `OTA_BEGIN`'s total counts both, and the application
  stores the envelope after the image in the external store. No new command id.
  Open question 5.
- **PROPOSED — one key for both.** One P-256 key per board id signs the
  trailer's span and the manifest's `Sig_structure`. The two to-be-signed formats
  cannot be confused: the trailer's begins with the magic `TFIM`, the
  `Sig_structure` with the CBOR array whose first member is `"Signature1"`. One
  key also means one element slot. Open question 4.

### 4. What the boot chain verifies — ACCEPTED

- **The same order, every boot, every slot:** trailer parse, `board_id`, floor,
  measurement, signature. On A/B this includes the fallback slot before the
  device falls back to it.
- **The bootloader is the authority.** The application's checks before
  activation (trailer, board id, floor, measurement, signature, manifest) only
  save a reboot into a refusal. Nothing the application decided is trusted at
  boot.
- **The device does not verify the CoRIM** (owner, 2026-10-15, answer 3). The
  control plane checks the image against it before sending, and the verifier
  appraises the device's reported `FWHash` against it afterwards.

### 5. Anti-rollback — ACCEPTED

- **One counter, the security version.** The trailer's `image_version` is the
  security version, and the manifest's sequence number equals it. The version
  for people is the application descriptor's major, minor and patch, which
  `APP_INFO` reports. Boot compares only the security version.
- **The floor advances at confirm, on every board with an update path.** A trial
  boot must not raise the floor, or the fallback image would fall below it and
  be refused. Every update path in this ADR has a fallback (decisions 6 and 7),
  so the shipped "advance on the first verified boot" rule leaves AVR with the
  in-place session it replaces.
- **How the floor advances on AVR.** `OTA_CONFIRM` sets `boot_state` to
  `CONFIRMED`. On the next reset, `tfw-boot` handles `CONFIRMED` before anything
  else: it advances the floor to the running image's version
  (`tfw_boot_floor_advance`, as shipped) and sets `GOOD`. A staged image is
  evaluated only after that, so an older one cannot slip in between confirm and
  reset.
- **`TFW_BOOT_FLOOR_MAX_STEP` (16) stays** on every counter that can be
  exhausted.
- **The counter, per tier:**

| Tier / part | Counter | Holds against |
|---|---|---|
| none (ATtiny85) | none: no secure boot | nothing |
| AVR element (ATECC608C), every shipped AVR board | the element's monotonic counter 0, `TFW_ATECC_COUNTER_BOOT` (shipped; the slot-map ADR keeps it) | code execution in the application; about 2 000 000 increments for the part's life |
| AVR soft | `rollback_floor` in reserved EEPROM | an old signed image replayed over the wire; **not** code on the device or ISP. No AVR board ships a soft-tier boot, and none can verify a signature in its boot section (decision 6) |
| SE052F, OPTIGA Trust M (future rows) | the element's monotonic counter object | as the ATECC608C |
| ESP32-P4 | the eFuse secure version, through ESP-IDF's anti-rollback | one-way bits; a small fixed number of steps for the part's life |
| Cortex-M33 under TF-M (STM32H5, nRF54L15, MCXN947, RP2350) | MCUboot's per-image security counter, in TF-M's non-volatile counters | code in the non-secure world |
| Cortex-M33 under option (a), Cortex-M7 without an SoC root of trust | a counter in OTP or secure flash, chosen in each port's ADR | code in the non-secure world, or outside the MPU region on the M7 |
| i.MX 95 M7 | pending #15; the facts found are in decision 12 | decision 12 |

### 6. Transports and the AVR network path — ACCEPTED (requirement), PROPOSED (budget and store)

Owner, 2026-10-15, answer 1, corrected at 16:15Z:

- Network OTA over TCP/IP (W5100 or W5500) is required on every board, the 328P
  included, beside serial.
- Where the two transports do not both fit, the board's image selects one at
  build time, by the object list and never by `#if`. Images carry both where
  they fit.
- A serial-attached host is rejected.
- The 328P path is the technically pure design, with numbers, and never a
  shortcut.

Measured with avr-gcc 14.2.0; the method is under
[Measurements](#measurements-method). Flash is `.text` plus `.data`; RAM is
`.data` plus `.bss`.

#### 6.1 Can the bootloader carry the network? No, on either part

| Boot build (element tier) | ATmega2560 (budget 8 192) | ATmega328P (budget 4 096) |
|---|---|---|
| shipped, no receive session (`BOOT_OTA=0`) | 3 730 | 3 572 (RAM 350) |
| serial session (`BOOT_OTA=1`, shipped on the 2560) | 6 840 (RAM 774), 1 352 free | 6 668, over by 2 572 |
| network session only, W5100 | 8 852, over by 660 | 8 476, over by 4 380 |
| network session only, W5500 | 8 920, over by 728 | 8 540, over by 4 444 |
| both sessions, W5100 | 8 928, over by 736 | 8 552, over by 4 456 |
| both sessions, W5500 | 8 992, over by 800 | — |
| network only, W5500, plus swap-move | 9 346, over by 1 154 | — |
| both, W5500, plus swap-move | 9 422, over by 1 230 | — |

The network session costs about 2 000 to 2 150 bytes in a boot section: the
W5x00 driver, the network configuration, and the TCP glue to the shipped
session. Neither part can hold it. So on both parts **the application receives
the image over the network**, and the bootloader takes it from there. The
application cannot write flash, so it stages into storage outside the MCU.

#### 6.2 The ATmega2560: serial in boot, network through the application

- **`tfw-boot`:**
  - the serial session, now writing the secondary slot;
  - swap-move A/B (decision 7);
  - a register-level read path from an external SPI store;
  - one trial (decision 9).
- **The application** (`ETH=1`):
  - receives `OTA_BEGIN`/`OTA_DATA`/`OTA_END` over Ethernet or serial, and
    streams the chunks into the external store;
  - runs the activation gate: trailer, `board_id`, floor, element SHA-256 over
    the staged bytes, element verify, and the manifest;
  - marks the image staged.
- **At the next reset `tfw-boot`:**
  - re-verifies the staged image from the store;
  - copies it into the secondary slot;
  - swap-moves;
  - boots it on trial.

| Boot build, ATmega2560 | Flash | RAM | Free of 8 192 |
|---|---|---|---|
| shipped `BOOT_OTA=1` (serial) | 6 840 | 774 | 1 352 |
| + swap-move | 7 264 (+424) | 1 030 (+256, one page buffer) | 928 |
| + external store read and copy (register-level SPI) | 7 560 (+296) | 1 158 (+128) | 632 |
| + verification of the staged image from the store, and the trial | **7 924** | 1 158 | **268** |
| the same with a store write path (not needed on the 2560: its fallback is the other slot) | 7 988 | 1 158 | 204 |
| for comparison: no serial session, swap-move, store and trial (`BOOT_OTA=0`) | 5 104 | 734 | 3 088 |

The serial session's retargeting from in place to the secondary slot is an
address change, not measured separately.

| Application, ATmega2560, element tier | Flash | RAM | Budget 65 536 |
|---|---|---|---|
| serial only (today) | 18 366 | 989 | fits |
| `ETH=1`, W5100 | 20 346 | 1 009 | fits |
| `ETH=1`, W5500 | 20 354 | 1 009 | fits |
| `ETH=1` W5500 + stager (about 1 205) + manifest reader (about 2 886) | about 24 445 | about 1 017 | fits; RAM is 7 under its 1 024 budget line, on an 8 192-byte part |

**Where every byte goes, ATmega2560** (byte addresses, 256-byte pages):

| Range | Size | Holds |
|---|---|---|
| `0x00000`–`0x100FF` | 257 pages, 65 792 | primary slot: the 65 536-byte image budget plus the one move page; the trailer ends at `0x0FFFF` |
| `0x10100`–`0x200FF` | 256 pages, 65 536 | secondary slot |
| `0x20100`–`0x3DFFF` | 122 624 | erased and unused by OTA; a later budget raise grows both slots into it, by a bench reflash |
| `0x3E000`–`0x3FFFF` | 8 192 | `tfw-boot`, 7 924 measured, 268 free |
| external SPI store | at least 65 536 + the manifest | the staged network image; no backup area, since the other slot holds the fallback |
| EEPROM, platform block | `boot_state` @48, `ota_state` @49 | slot and trial states (decision 9) |
| EEPROM, after the network block | proposed `0x0E0`: the swap record | swap phase and step, so a power cut resumes the swap (decision 7) |

The slot size is open question 7.

#### 6.3 The ATmega328P: four candidates, one design

The 328P boot section cannot hold even the serial session (6 668 bytes against
4 096), so the 328P has no update path today. The owner's four candidates:

| Candidate | Flash | RAM | Hardware | Attack surface | Verdict |
|---|---|---|---|---|---|
| **1. The application stages; the boot verifies.** Into internal flash it needs `SPM` from the application. Into EEPROM (1 024) or SRAM (2 048) it does not fit a 28 672-byte image. | — | — | none | an `SPM` entry voids the BLB1 read barrier | infeasible alone; with an external store it becomes candidate 2 |
| **2. External SPI storage on the shield bus.** The application writes; the boot reads. | boot: +406 with a register-level driver (3 978); +744 with the HAL's SPI (4 316, over by 220). Application: stager about 1 205 | boot +128 (page buffer); application +8 | an SPI NOR of at least 512 Kbit on its own chip select, or the shield's microSD socket | the boot reads bytes an attacker with the board can write; it executes none of them before the authoritative evaluation of the internal copy | **taken** |
| **3. A split boot that only verifies and copies.** No receive session in the boot section. | see the variants below; the leanest is 3 924 | 478 | none beyond 2 | no parser of network input in the boot; its inputs are the store's bytes and two EEPROM state bytes | **taken** |
| **4. The element hashes and verifies.** SHA-256 and P-256 Verify in the ATECC608C. | shipped in the 3 572 base; the verify backend is 204 bytes. Software SHA-256 alone would add about 3 450, and software Ed25519 does not fit at all (decision 3) | none | the ATECC608C (MIKROE-6774), already the tier | the I²C bus to the element: an interposer can answer "verified", as on every element-tier boot today | **taken**; the element cannot hold the image (its data zone is about 1.2 KB) |

The design is 2, 3 and 4 together. The boot variants measured:

| 328P boot variant | Flash | Free of 4 096 |
|---|---|---|
| shipped `BOOT_OTA=0` | 3 572 | 524 |
| + store read and copy, register-level SPI | 3 978 | 118 |
| + store write (backup taken by the boot) | about 4 292 | over by 196 |
| + backup by the boot, verification of the staged image in the store, trial | 4 504 | over by 408 |
| + verification in the store, trial; backup taken by the application | 4 440 | over by 344 |
| **copy, then the shipped authoritative evaluation of the internal copy, trial, and restore of the backup on a reset during the trial; backup and pre-check taken by the application** | **3 924** (RAM 478) | **172** |

The last row is the design. It leaves out one piece of glue: restoring the
backup when the evaluation of the new copy refuses it. That glue reuses the
measured copy routine and is estimated at tens of bytes, inside the 172 free.

How it runs:

- **The application** (`ETH=1`; it already links the W5x00 driver and the
  auth session):
  1. On `OTA_BEGIN`, back the running image up to the store's `PREV` area.
  2. Stream `OTA_DATA` into the store's `STAGE` area, with no page buffer: NOR
     programs partial pages.
  3. On `OTA_END`, run the gate: trailer, `board_id` `0x01`, floor, element
     SHA-256 over the staged bytes, element verify, and the manifest where the
     budget allows.
  4. On `OTA_ACTIVATE`, mark the image `PENDING`.
- **`tfw-boot`**, on a reset that finds `PENDING`:
  1. Copy `STAGE` into the application region. A power cut leaves `PENDING`, and
     the copy repeats; it is idempotent.
  2. Write `TRIAL`.
  3. Evaluate the internal copy, the shipped way.
  4. If the evaluation refuses, copy `PREV` back and mark `FAILED`.
- **A reset in `TRIAL`** (deadline, watchdog or crash) also copies `PREV`
  back, marks `FAILED`, and evaluates `PREV` as every boot does.

**Where every byte goes, ATmega328P:**

| Range | Size | Holds |
|---|---|---|
| `0x0000`–`0x6FFF` | 28 672 | application region; the trailer is its last 128 bytes, `0x6F80`–`0x6FFF`. The image is at most 28 544 bytes |
| within it, under the 16 384 policy line | 16 384 | today's budget: the serial image is 16 076 |
| `0x7000`–`0x7FFF` | 4 096 | `tfw-boot`, 3 924 measured, 172 free |
| store `STAGE`, `0x000000` | 28 672 (seven 4 KB sectors) + the manifest | the staged image |
| store `PREV`, `0x008000` | 28 672 | the backup of the running image |
| EEPROM, platform block | `boot_state` @48, `ota_state` @49 | proposed new values extend the enumerations, and none renumbers: `boot_state` gains `TRIAL` (3) and `CONFIRMED` (4); `ota_state` gains `PENDING` (4) |

**Both budgets.** The boot section is 4 096 bytes under either budget. The
application:

| 328P application, element tier | Flash | Under 16 384 (policy) | Under 28 544 (region less trailer) |
|---|---|---|---|
| serial only, no OTA (today) | 16 076 | fits, 308 free | fits |
| serial + stager (OTA over serial only) | about 17 281 | **over by about 897** | fits |
| network only (`ETH=1` less the UART: its two ISRs and receive, 192 bytes by symbol size) | about 17 862 | **over by about 1 478** | fits |
| both, `ETH=1`, W5100 | 18 054 | **over by 1 670** | fits |
| both, `ETH=1`, W5500 | 18 062 | **over by 1 678** | fits |
| both, W5500 + stager | about 19 267 | over | fits, about 9 277 free |
| both, W5500 + stager + manifest reader | about 22 153 | over | fits, about 6 391 free |

RAM: `ETH=1` is 943 of the 1 024 budget line. The stager adds 8 bytes; the
manifest reader works on 64-byte blocks on the stack.

**The network path needs the 28 672 region budget.** Under the 16 384 policy
line the 328P has no OTA path on either transport: serial plus stager is over
by about 897, and the network image by about 1 478 or more. Raising
`TFW_FLASH_BUDGET` toward the region is option (a) of the Ethernet ADR
([../ethernet-transport/adr-wire-protocol-over-w5x00.md](../ethernet-transport/adr-wire-protocol-over-w5x00.md)),
which the owner has not chosen yet. This ADR depends on it: open question 1.

**The external store.** The measured driver assumed an SPI NOR (READ `0x03`,
WREN `0x06`, 4 KB sector erase `0x20`, page program `0x02`, status `0x05`) on
its own chip select, with the W5x00's chip select (D10) parked high. The store
needs at least 57 344 bytes plus the manifest, so 512 Kbit is the smallest part.
Ethernet shields that carry a microSD socket wire its chip select to D4. A board
uses either that socket (raw block access, whose code is not measured) or a NOR
on its own chip select. The chip select is board data in `board_profile.h`.
Open question 2.

#### 6.4 The ATmega32U4 and the ATtiny85

- **ATmega32U4.** It has the same geometry as the 328P (a 4 096 boot section and
  a 28 672 region), so the 328P design applies unchanged. It is not measured,
  and no Ethernet controller is wired: `boards/atmega32u4/profile.mk` names no
  `ETH_CHIP`. Its 14 336 budget line is tighter than the 328P's, so it too needs
  the region budget. Follow-up.
- **ATtiny85.** It has no boot section, so it has no secure boot, no verified
  update and no Ethernet controller (8 KB, no `ETH_CHIP`). The owner's "every
  board" cannot hold here without a boot section, so this ADR proposes the
  ATtiny85 as the one board without OTA: open question 3.

#### 6.5 Transport per image

| Board | Image | Boot carries | Application carries | Ships |
|---|---|---|---|---|
| ATmega2560 | `ETH=1`, element tier | serial (7 924 with swap, store and trial) | serial **and** network (about 24 445 with stager and manifest) | **both**: network through the application, serial through the application or the boot session |
| ATmega2560 | `ETH=0` (no shield) | serial | serial | serial in the boot session; the application's serial path needs the store, which sits on the shield bus |
| ATmega328P | `ETH=1`, region budget | none: verify and copy (3 924) | serial **and** network (about 22 153) | **both**, through the application |
| ATmega328P | 16 384 policy line | none | serial, no OTA | **no OTA**: no transport fits (6.3) |
| ATmega32U4 | as the 328P | as the 328P | as the 328P | as the 328P, unmeasured |
| ATtiny85 | — | — | — | no OTA (open question 3) |

No measured image has to choose one transport. Wherever OTA fits at all, both
transports fit, because the TCP link already keeps the UART. If a board appears
where only one fits, its image links one link file, chosen by its profile, as
`ETH` chooses today. That costs a third link file, not an `#if`.

### 7. Slot layout per arch class — ACCEPTED

Owner, 2026-10-15, answers 2 and 12.

- **ATtiny85: none** (open question 3).
- **ATmega328P and ATmega32U4: one internal slot, with the backup in the
  external store** (6.3). This is not A/B: the fallback is the copy in `PREV`,
  which the boot restores, and the boot evaluates it again before it runs.
- **ATmega2560: A/B by swap-move.**
  - The primary slot is one page larger than the secondary. Swap-move shifts the
    primary up one page, then swaps page by page.
  - Progress (phase and step) is recorded in EEPROM, so a power cut resumes the
    swap. Measured at +424 bytes and a 256-byte page buffer.
  - Each page is erased about twice per update and twice more per fallback.
    AVR flash is rated for 10 000 cycles, so about 5 000 updates.
  - Images stay linked for address 0. The secondary slot lies above 64 KB, so
    the boot HAL reads it with far reads.
  - Moving a Mega to A/B changes its bootloader, so it is a bench reflash, which
    the owner accepts (answer 2).
- **Cortex-M33 under TF-M: MCUboot's primary and secondary slots,** written
  through PSA Firmware Update. MCUboot's test-and-confirm gives the trial and the
  fallback. Composed, never reimplemented (decision 11).
- **Cortex-M `tfw-boot` ports: A/B.** A hardware bank swap where the part has
  one (the STM32H5's dual bank), swap-move otherwise. The geometry is boot-HAL
  data.
- **ESP32-P4: ESP-IDF's `ota_0`/`ota_1` and `otadata`, and its app rollback.**
  Composed, never reimplemented.

### 8. Wire commands — ACCEPTED (status code, RBAC, Ethernet), PROPOSED (ids and layouts)

Owner, 2026-10-15, answers 8 and 10.

**Every board with an update path** serves these in the application:

| Id | Command | Request | Response | Served by |
|---|---|---|---|---|
| `0x68`–`0x6B` | `OTA_BEGIN`, `OTA_DATA`, `OTA_END`, `OTA_STATUS` | unchanged | unchanged | the application, writing the external store (AVR) or the inactive slot; the 2560's boot session as well, over serial |
| `0x6C` | `OTA_INFO` | `[slot:1]` | `[slots:1][active:1][flags:1][floor:4][state:1][image_version:4][measurement:32]` (44 bytes) | the application |
| `0x6D` | `OTA_ACTIVATE` | `[mode:1]`: 0 at the next reset, 1 reset now | none | the application |
| `0x6E` | `OTA_CONFIRM` | none | none | the application, in `TRIAL` only |

- `0x6F` stays unallocated. `OTA_END` keeps its meaning: every byte arrived,
  nothing verified.
- `OTA_INFO`'s `state` is the slot state of
  [architecture.md](architecture.md#device-state-machine). Its `flags`:
  - bit 0: the last boot fell back;
  - bit 1: the fallback followed a refusal at evaluation (clear: the trial
    ended unconfirmed);
  - bit 2: the device is in `TRIAL`.
- **A new status code** (owner, 2026-10-15, answer 10): `TFW_ERR_BAD_STATE`, "the
  command is not valid in the current OTA state". Its value is the next free one
  when it lands, 19 today. An older host reads it as unrecognised. It answers:
  - activate with no verified image;
  - activate of the image that last failed its trial (decision 9);
  - confirm outside `TRIAL`;
  - begin while an image is pending.
- **RBAC** (owner, 2026-10-15, answer 8): `OTA_ACTIVATE` and `OTA_CONFIRM` are
  the permissions `ota.activate` and `ota.confirm` in the platform's RBAC model,
  in qrdb's vocabulary, tenant-scoped and carried by the default roles. The
  control plane checks them before it sends either command. The device enforces
  the operator auth session (`AUTH_CHALLENGE` `0x61`, `AUTH_RESPONSE` `0x62`) on
  the element tier, as for `RESET` mode 1, and answers `TFW_ERR_AUTH_REQUIRED`
  (17) without one.
- **The auth session over Ethernet** (owner, 2026-10-15, answer 8). It works on
  the TCP link as on serial: the session is frame-level and link-independent.
  The Ethernet allow-list widens from `0x00`, `0x06`, `0x60`, `0x63`, `0x64` to
  add `0x61`, `0x62` and `0x68`–`0x6E`. **PROPOSED:** a session opened on a
  connection ends when that connection closes, as well as at reset, so a
  reconnecting peer starts unauthenticated (open question 8).
- A host tells an OTA-capable image by `OTA_INFO` answering. An image without it
  answers `TFW_ERR_UNKNOWN_CMD`. There is no `CAPS` bit, following `APP_INFO`.
- On an M33 under TF-M the application maps these commands onto PSA Firmware
  Update calls; the wire is the same.

### 9. The device state machine: one trial, confirmed by the control plane — ACCEPTED

Owner, 2026-10-15, answer 7 (the control plane confirms, after attestation) and
answer 9 (16:25Z: one trial boot with a per-board timeout; a failed trial rolls
back; recovery is another OTA round; the same image gets no second trial).

The machine is drawn in
[architecture.md](architecture.md#device-state-machine). The decisions in it:

- **One trial.** The trial is armed before the jump. Any reset that finds
  `TRIAL` ends it as failed: the deadline, the watchdog, a crash or a power cut.
  There is no attempt counter.
- **The control plane confirms.** The new image runs `init` and
  `tfw_app_after_update`. If both return `TFW_OK`, it opens its connection and
  reports. go-trusted-platform attests the device, appraises the new `FWHash`
  against the CoRIM, and only then sends `OTA_CONFIRM`. A device-local confirm
  is gone. `tfw_app_after_update` refusing ends the trial at once, as failed.
- **Where the timeout is configured.** A per-board constant in
  `boards/*/board_profile.h`, `TFW_OTA_TRIAL_TIMEOUT_S` (name PROPOSED, value
  per board PROPOSED, open question 6). It must cover a boot, the connection,
  one attestation and the confirm. It is enforced in two layers:
  - the application's tick counts the deadline and, at expiry, resets into
    `tfw-boot`;
  - `tfw-boot` arms the hardware watchdog before the trial jump, and the
    application SDK pets it during the trial, so a hung image is reset too.
  - On AVR the watchdog's longest period is 8 s, which is why the deadline
    itself is the application's.
- **What a failed trial reports.** The rolled-back image sets `OTA_INFO`'s
  `flags`: bit 0, and bit 1 if the evaluation refused. `state` is `FAILED`, and
  `image_version` and `measurement` are the failed image's. Its evidence carries
  the old `FWHash`. The control plane learns all of this on the device's next
  connection: it reads `OTA_INFO` before anything else, and attestation shows the
  image it did not send. The failed measurement is kept (PROPOSED: 32 bytes in
  the platform block's reserved range, open question 10). `OTA_ACTIVATE` of the
  same measurement answers `TFW_ERR_BAD_STATE`. That is a policy check in the
  application, not a boot check; the same image gets no second trial.
- **How the next round starts.**
  - On the ATmega2560, from the previous image, which the swap-back left in the
    primary slot. It is evaluated before it runs, and the next round is an
    ordinary `OTA_BEGIN` over either transport.
  - On the ATmega328P, from the `PREV` copy the boot restored. It is the full
    previous application, with the network and the stager, so the next round
    starts exactly as the first.
  - If `PREV` itself fails evaluation (a store corrupted by a compromised
    application, or an interrupted restore that cannot complete), nothing
    runnable is left. The 328P halts in `recovery_halt` and needs a programmer.
    A compromised application can destroy `PREV`; this is the design's
    residual risk, and the price of a 4 KB boot section.
- **Confirm and the floor.** `OTA_CONFIRM` sets `CONFIRMED` and disarms the
  deadline. The boot advances the floor at the next reset (decision 5).

### 10. Keys, custody and the provisioning seam — ACCEPTED (custody, per-board keys), PROPOSED (one key for both formats)

Owner, 2026-10-15, answers 4 and 5.

- **Custody.** The image key and the manifest signer are held in go-xkms and
  reached through go-quicbaas. They are hardware-backed, and the signatures are
  COSE. The build hands go-xkms the trailer's 48 to-be-signed bytes and the
  manifest's `Sig_structure`; it gets back the trailer's raw `r‖s` and the
  `COSE_Sign1`. No private key lives in this repo or in `sdk/go`.
- **One image key per board id.** An image signed for one board fails on another
  even before `board_id` is read. Rotating a board's key is a bench
  reprovisioning, because the element's slot is locked.
- **The provisioning seam row is go-xkms's** (DS-1902). It places the
  verification key in the element's image-signing slot: slot 2 today, slot 15 on
  the TrustFLEX map the
  [slot-map ADR](../device-provisioning/adr-trustflex-608c-slot-map.md)
  proposes. go-xkms must expose, per board id:
  - a P-256 key created in hardware whose private part never leaves;
  - export of the public key as the raw 64-byte `X‖Y` the element's slot takes,
    and as a `COSE_Key`, with the key id;
  - signing of a caller's prehashed 48-byte trailer span (raw `r‖s`), and
    `COSE_Sign1` over a manifest;
  - a record of which key id is pinned to which board id.

  This project's side: `tfwprov` writes the exported key into the slot on the
  bench and locks it, and never sees a private key. The row could not be read on
  go-xkms's main for this ADR, so it is not mirrored in the SPEC; the follow-up
  is to mirror it once it lands.

### 11. Frameworks first, per arch class — ACCEPTED

Owner, 2026-10-15, answer 12.

| Class | Framework | Our code |
|---|---|---|
| Cortex-M33 (STM32H5, nRF54L15, MCXN947, RP2350) | TF-M with MCUboot as BL2 and PSA Firmware Update: slots, swap, test and confirm, security counters | the wire mapping of `0x68`–`0x6E` onto PSA FWU in the non-secure application, and the manifest gate; pending the trust-model ADR (open question 9) |
| ESP32-P4 | ESP-IDF `esp_ota_*`, `otadata`, app rollback, secure boot v2's signature, eFuse secure version | our trailer v1 (`board_id` `0x80`) beside the vendor's signature, checked by our code before `esp_ota_set_boot_partition`; the manifest gate; the wire mapping. The esp32 port's ADR (#16) states where the trailer sits in ESP-IDF's image |
| i.MX 95 M7 | NXP AHAB and the EdgeLock Enclave, in trusted-imx95's container | none until #15 (decision 12) |
| AVR | none exists for a 4 KB boot section with an element | `tfw-boot` (decisions 2–9) |
| All | — | the common abstraction: the trailer, the manifest profile, the `OTA_*` wire, `sdk/go`'s upload helper |

### 12. The i.MX 95 M7 — PROPOSED, decided with #15

Owner, 2026-10-15, answer 11: decided with the trust-model ADR (#15), and no M7
command until then. The CA's recommendation, recorded for that decision:

- AHAB and the ELE verify the M7 container; there is no trailer check.
- A two-level floor: the fuse-backed AHAB version, bumped only for revocation,
  plus a per-release monotonic version in enclave secure storage, advanced at
  confirm.
- The A-core agent stages into the inactive container slot. The ROM's
  primary/secondary fallback applies, and the boot pointer flips only after the
  control plane's confirm.

**What the vendor documents confirm, and what they do not.** The device is the
NXP FRDM-IMX95-PRO. These facts were checked on 2026-10-15 (#52) against NXP's
documents from trusted-imx95's `docs/references/`, read from NXP's public copies:

- UM12527, *FRDM-IMX95-PRO Board User Manual*, Rev. 1.0, 22 June 2026;
- RM00284, *EdgeLock Enclave Hardware Security Module API*, Rev. 4.5,
  24 September 2026;
- IMX95CEC, the i.MX 95 commercial data sheet, Rev. 8, 7 April 2026. Its
  revision was not compared with trusted-imx95's copy.

AN14471 and AN14861 are in that set, but they cover the i.MX RT1170 and RT1180,
so nothing here is cited from them. The i.MX 95 Reference Manual (IMX95RM) and
the Security Reference Manual (SRM) are not in the set. Rows that depend on
them say "needed from". The SPSDK and U-Boot rows come from public sources
outside the set and are marked that way.

| Fact | Status | Source |
|---|---|---|
| The board's processor is the MIMX9596AVZXNAC: up to six Cortex-A55, one Cortex-M7 and one Cortex-M33 | verified | UM12527 Rev. 1.0, §1.2 Table 1 |
| The M7 runs at up to 800 MHz with 512 KB of TCM; the M33 at up to 333 MHz with 512 KB of TCM | verified for the family | IMX95CEC Rev. 8, Table 1. The board's part is not among IMX95CEC's commercial part numbers. Its own figures are needed from the data sheet that lists it; UM12527 §6 names IMX95IEC |
| Memories: one 16 GB LPDDR5, Micron MT62F4G32D8DV-023FAIT (U21); 32 GB eMMC 5.1, Micron MTFC32GBCAQTC-IT (U19), on uSDHC1; a microSD slot (J1) on uSDHC2; 512 Mbit 1.8 V quad SPI NOR, Winbond W25Q512NWEIQ (U16), on XSPI | verified (settles tf-52's row) | UM12527 Rev. 1.0, §1.2 Table 1, §2.6, §2.8, §2.9, §2.10 |
| Boot devices: eMMC on uSDHC1 (**the default**), microSD on uSDHC2, FlexSPI serial NOR, and USB serial download (on USB1 by default) | verified | UM12527 Rev. 1.0, §1.2 Table 1, §2.3 Table 7 |
| SW4 is a 4-position DIP switch; SW4[1..4] drive `BOOT_MODE3..0`, with ON = 1. `x001` is the serial downloader, `x010` eMMC, `x011` SD and `x100` FlexSPI NOR. The table names the Cortex-M33 as the boot core | verified | UM12527 Rev. 1.0, §1.6, §2.3 Table 7 |
| FlexSPI NOR is `x100`: SW4[2] ON, SW4[3] and SW4[4] OFF, SW4[1] either. `1000` is wrong | verified (settles tf-52's row) | UM12527 Rev. 1.0, §2.3 Table 7 |
| Holding K2 at power-up or reset boots from microSD instead of eMMC, without changing SW4. K1 is a PMIC cold reset that leaves `NVCC_BBSM_1P8` powered | verified | UM12527 Rev. 1.0, §1.7 Table 4 |
| Debug: a CH9114F (U67) behind USB-C J22 gives four UARTs at 115200 8N1, one each for the A55, the M33 and the M7 | verified | UM12527 Rev. 1.0, §1.2 Table 1, §2.19 |
| Which COM port belongs to which core | **not fixed**: the manual says to open all four. The M7's port is found by its output | UM12527 Rev. 1.0, §2.19 (settles tf-52's row) |
| `hsm_commit()` writes the SRK revocation and firmware version from the NXP and OEM containers into fuses. The OEM bits are `HSM_COMMIT_OEM_FW_VERSION` and `HSM_COMMIT_OEM_SRK_REVOCATION`. Only the boot ELE firmware offers it | verified | RM00284 Rev. 4.5, §3.35 |
| The AHAB container header has an 8-bit `fuse_version`, which must be at least the fused value, and a 16-bit `sw_version`, which picks between images and **is not a floor** | public, outside the set; field widths and meaning needed from IMX95RM / SRM (AHAB chapter) | SPSDK AHAB documentation; U-Boot `doc/imx/imx95_container.txt` |
| SPSDK's anti-rollback example commits with `nxpele commit -i oem_fw_fuse` | public, outside the set; consistent with RM00284 §3.35 | SPSDK, *i.MX95 Anti-rollback Protection Example* |
| Which fuse word backs the OEM container's fuse version, and how many steps it allows | **needed from the i.MX 95 Security Reference Manual (fuse map) / IMX95RM** | — |
| The M7 image is an optional `cortex_m7_app` in the OEM container of the primary container set, loaded to `0x303C0000`, beside the System Manager and U-Boot SPL. **It shares that container's fuse version** | public, outside the set; needed from IMX95RM (System Boot and AHAB chapters) | SPSDK, *i.MX 95 AHAB with U-Boot* |
| The ELE writes the key store to NVM only after a strict operation. Key-store updates count once the strict operation is acknowledged. The monotonic counter is limited, so RM00284 advises batching updates before a strict one. Once the counter is spent, each export warns that updates are not roll-back protected | verified | RM00284 Rev. 4.5, §2.4, §2.4.2 |
| The counter is reached only through flags on key-store operations, never by a call of its own. Key generation and import: `HSM_OP_KEY_GENERATION_FLAGS_MONOTONIC_COUNTER` and `..._STRICT_OPERATION` (§3.2.3.10, §3.2.3.11, §3.2.4.2). Key groups: `HSM_OP_MANAGE_KEY_GROUP_FLAGS_MONOTONIC` and `..._SYNC_KEYSTORE` (§3.2.3.14, §3.2.3.15). The key-store service: `HSM_SVC_KEY_STORE_FLAGS_MONOTONIC` and `..._STRICT_OPERATION` (§3.22.3.4, §3.22.3.5). Key exchange: §3.20.3.7, §3.20.3.8 | verified | RM00284 Rev. 4.5 |
| `hsm_dev_mono_counter()` splits the counter's fuse words between the ELE HSM, V2X HSM and V2X SHE, once per device. With no such OTP configuration, a counter operation warns and applies no roll-back protection. `hsm_get_info()` returns `chip_monotonic_counter` (16 bits) | verified | RM00284 Rev. 4.5, §3.24, §3.19 |
| How many monotonic-counter fuse words the i.MX 95 has, and so how many strict updates a device can make | **needed from the i.MX 95 Security Reference Manual (fuse map)** | — |
| The API offers **no general monotonic counter to software**: nothing increments or reads a counter for a value of the caller's own | verified, by absence across the API | RM00284 Rev. 4.5, §3 |
| Data storage (`hsm_data_storage`, `hsm_enc_data_storage`, `hsm_data_ops`, `hsm_enc_data_ops`) stores and retrieves blobs by `data_id` in "the Linux filesystem managed by" the ELE firmware. **Its flags carry no strict or monotonic bit, and RM00284 claims no roll-back protection for it.** tf-52's "encrypted under a device-derived key" is not in RM00284 and is withdrawn | verified | RM00284 Rev. 4.5, §3.8, §3.8.5.1, §3.8.5.2 |
| `hsm_dev_attest()` and `hsm_dev_getinfo()` report the OEM SRK revocation status (uncommitted changes excluded), the firmware hash and the lifecycle, but no firmware version | verified | RM00284 Rev. 4.5, §3.12, §3.13 |
| `crrm_get_boot_mode()` and `crrm_set_boot_mode()` hold a boot mode in the BBSM: normal `0x69`, recovery download `0x3c`, recovery install `0x5a`. Only download→install, install→normal and install→download are allowed, one per boot cycle. It is a recovery mode, not a trial-boot pointer | verified | RM00284 Rev. 4.5, §3.25 |
| The ELE identifies a caller by its domain ID and messaging unit | verified | RM00284 Rev. 4.5, §3.22.1 |
| Whether the M7 has a messaging unit to the ELE | **needed from IMX95RM** | — |
| The ROM has a secondary boot: from eMMC, the other boot partition; from other devices, an image offset. U-Boot reads the boot stage (secondary `0x9`) and the offset from the ROM's passover data | public, outside the set; needed from IMX95RM (System Boot chapter) | U-Boot patch *imx9: scmi: Support iMX95/94/952 secondary boot* (2026-04) |
| What triggers the fallback (authentication or load failure), the i.MX 95's secondary-offset fuse, and whether software can force a one-time secondary boot | **needed from IMX95RM, System Boot chapter** (UM12527 §2.3 Table 7 points there) | — |
| On eMMC, the boot pointer is the device's `BOOT_PARTITION_ENABLE` (`EXT_CSD[179]`) | verified as the eMMC mechanism | JEDEC JESD84-B51 |
| Whether the i.MX 95 ROM boots the enabled partition first | **needed from IMX95RM, System Boot chapter** | — |

**What the facts mean for the recommendation**, for #15 to weigh:

- **"AHAB and the ELE verify the M7 container" holds** as far as the set goes.
  The ELE commits the OEM container's firmware version and SRK revocation
  (RM00284 §3.35). The container format itself needs IMX95RM / SRM.
- **The fuse-backed floor holds.** It is **per container set, not per M7
  image** (public SPSDK, to confirm against IMX95RM). A bump raises the floor
  of the System Manager and the SPL as well, which fits "bumped only for
  revocation". How many bumps a device allows needs the SRM's fuse map.
- **The per-release version "in enclave secure storage" does not hold as
  written.** ELE data storage has no counter. It lives in the A-core's Linux
  filesystem, and RM00284 makes no roll-back claim for it (§3.8). Only
  key-store exports made under `STRICT_OPERATION` with the monotonic flag are
  counter-protected (§2.4.2).

  *Inference, not stated by RM00284:* a version becomes roll-back resistant if
  it is tied to the key store. For example, a key or key group could be
  rotated at each confirm and exported under strict operation with the
  monotonic flag, with the M7 checking the version against it. Each release
  then spends one counter step. The steps come from fuse words that
  `hsm_dev_mono_counter()` assigns once per device (§3.24), and their number
  needs the SRM. The other choice is to keep only the AHAB floor and accept
  roll-back within a fuse level. #15 decides.
- **Updating the M7 image means updating the boot container set**, which
  trusted-imx95 owns. The alternative is to load the M7 later, from its own
  container, authenticated through the ELE. That also needs the M7, or the
  core that loads it, to reach an ELE messaging unit (IMX95RM). #15 decides.
- **"The A-core agent stages into the inactive slot" fits the board.** The
  default boot device is the eMMC on uSDHC1 (UM12527 §2.3, §2.9). That the
  A55's Linux writes it is an inference, not stated in the manual. The slot
  layout and how the ROM chooses between slots need IMX95RM.
- **"Flip the pointer only after confirm"** needs a way to boot the staged
  container once without flipping. The ELE's boot mode (RM00284 §3.25) is a
  recovery path, not that. A one-time secondary boot needs IMX95RM. Without
  one, the trial runs after the flip, with the ROM's fallback as the safety
  net.

Open question 7 of the
[trust-model ADR](../cortex-m/adr-trust-model-vs-tf-m.md#open-questions-for-the-owner)
carries the same facts; it stays open.

### 13. The `sdk/go` upload helper — ACCEPTED (shape), PROPOSED (names)

Proposed names, not implemented. Standard library only; no private key; no
signature verification, since that is the device's.

| Name | Does |
|---|---|
| `tfw.ParseImage(b []byte) (*Image, error)` | checks structure only: whole pages, trailer magic and version 1, length, `board_id` non-zero; splits off an appended manifest envelope |
| `(*Device).Upload(ctx, *Image, ...UploadOption) error` | `OTA_BEGIN`, stop-and-wait `OTA_DATA` in chunks that fit `TFW_MAX_PAYLOAD`, `OTA_END`; resumes within a session from `OTA_STATUS`'s received count |
| `(*Device).OTAInfo(ctx, slot byte) (*SlotInfo, error)` | `OTA_INFO` |
| `(*Device).Activate(ctx, now bool) error` | `OTA_ACTIVATE` |
| `(*Device).Confirm(ctx) error` | `OTA_CONFIRM` |

`tfwctl` gains an `ota` verb over the same calls. go-trusted-platform calls the
helper on the device's connection, serial or TCP; it never encodes an `OTA_*`
frame itself.

## Measurements: method

Measured with avr-gcc 14.2.0, on the tree this ADR branches from. The
throwaway prototypes were never committed. They were:

- the TCP session glue;
- swap-move;
- a register-level SPI store driver, read and write;
- the boot's trial logic, in three variants;
- the application's stager;
- a SUIT and `COSE_Sign1` reader.

Only these numbers and this method are kept.

- **Boot images.** `make -n -B bootloader` was run for each shipped
  configuration (ATmega328P and ATmega2560; element tier for tier, measure and
  signature; `BOOT_OTA` 0 and 1). From its dry run the compile and link lines
  were taken. Each prototype was compiled with exactly those flags:
  - compile: `-std=c11 -Os -flto -ffunction-sections -fdata-sections
    -fno-fat-lto-objects -Wall -Wextra -Werror -Wconversion …`;
  - link: `-Wl,--gc-sections -Wl,--relax`, the hand-off linker script and the
    boot section's start.
  - Entry points were kept with `-Wl,-u,<symbol>`.
  - A build that overflows its section was linked at address 0, so its size is
    read rather than refused.
  - The network session replaced the boot's UART receive and transmit with
    TCP equivalents at link time (`-D` on the one session file).
- **Applications.** `make firmware BOARD=<part> CRYPTO=atecc608a [ETH=1]
  [ETH_CHIP=w5500]`, whose size check prints flash and RAM. These are the
  current figures; the Ethernet ADR's slightly older ones differ by under 20
  bytes.
- **The network-only 328P application.** No link file exists for it, so it was
  estimated from `ETH=1` by symbol size: the UART's two ISRs and its receive
  routine, 192 bytes; LTO inlined the rest.
- **The stager.** About 1 205 bytes, unlinked, for the 328P: begin with backup,
  data, end with the gate.
- **The manifest reader.** Linked against stubs, less the stubs' own size:
  about 2 886 bytes on both parts. The unlinked object is 3 222.
- **Software Ed25519 for a soft-tier AVR boot.** ed25519-donna links at 72 930
  bytes. Monocypher 4.0.2's `crypto_ed25519_check` links at 50 340 bytes of
  text, with a 1 077-byte stack frame, on the 2560. Neither fits a boot
  section; software SHA-256 alone is about 3 450.
- Sizes are `.text` + `.data` for flash and `.data` + `.bss` for RAM, from
  `avr-size`.

## Seams: what the other repos must expose

These are requirements stated for those repos, in their terms. Filing them is
those repos' work; this ADR only reports them.

### go-trusted-platform

> **device-ota** — The control plane delivers firmware images to
> trusted-firmware devices through its device-facing endpoint, on the
> connection the device opens, over TCP (the W5x00 link) or serial. It owns
> fleet policy and rollout: which image a device gets, keyed by board id,
> application and device profile; the waves; and pausing a rollout on a failure
> rate. It learns a device's application, profile and version from `APP_INFO`
> (`0x06`) and its OTA state from `OTA_INFO` (`0x6C`), through `sdk/go`. Before
> sending an image it fetches the image, its signed SUIT envelope and its CoRIM
> from go-quicbaas's reference-values store, and refuses one whose digest does
> not match. It drives the transfer through `sdk/go`'s upload helper and never
> encodes the frames itself. It sends `OTA_ACTIVATE` and `OTA_CONFIRM` only for
> a principal holding `ota.activate` or `ota.confirm` in the device's tenant,
> inside the device's operator auth session.
>
> **control-plane confirm** — After an update it attests the device within the
> board's trial timeout and appraises the new `FWHash` through go-quicbaas. It
> sends `OTA_CONFIRM` only when the appraisal passes. A device reporting a
> failed trial (`OTA_INFO`), or the old measurement, is recorded, and the
> rollout acts on it. Recovery is a new round; the same image is never
> activated twice on a device.
>
> trusted-firmware's SPEC (`ota`) names the same seams.

This widens requirement R1 of the
[onboarding architecture](../device-onboarding/architecture.md#requirements-on-other-repositories),
which today has the endpoint issue only `CAPS` and the three attestation
commands. It now issues `AUTH_CHALLENGE`, `AUTH_RESPONSE` and `0x68`–`0x6E` too
(owner, 2026-10-15, answer 8).

### go-quicbaas

> **firmware-reference-values** — The reference-values store holds, for each
> trusted-firmware image, the image bytes, its signed SUIT envelope and a signed
> CoRIM whose reference value is the image's SHA-256 measurement. Each is keyed
> by board id, application and security version. An entry is immutable once
> written. The store refuses an image whose digest differs from its CoRIM's or
> its envelope's. It serves the image and envelope to go-trusted-platform, and
> the CoRIM to the verifier that appraises a device's `FWHash`. It is the path
> through which the image build reaches go-xkms's signer. trusted-firmware keeps
> no copy and no store of its own. trusted-firmware's SPEC (`ota`) names the
> same seam.

### go-xkms

The provisioning seam row is go-xkms's (DS-1902; decision 10 states what it
must expose). The image key and the manifest signer are also go-xkms's, as are
CoRIM encoding and signing and the appraisal of a DICE `FWHash` against a CoRIM
reference value. A gap there is filed against go-xkms.

### qrdb

> **ota-permissions** — The RBAC vocabulary carries `ota.activate` and
> `ota.confirm`, tenant-scoped, granted by the default roles that today manage
> devices. go-trusted-platform checks them before it sends `OTA_ACTIVATE` or
> `OTA_CONFIRM`. trusted-firmware's SPEC (`ota`) names the same seam.

## Rejected alternatives

| Alternative | Rejected because |
|---|---|
| A serial-attached host that uploads for the control plane | Rejected by the owner (2026-10-15, answer 1). Every board takes its image over the network itself. |
| Trailer version 2 | Rejected by the owner (2026-10-15, answer 6, 16:15Z). Version 1 gains `board_id` in a byte today's parser ignores, and the boards are reflashed. |
| The vendor's signature only, on composed platforms | Rejected by the owner (2026-10-15, answer 6). Our trailer rides beside it, so the four facts have one form on every class. |
| Device-local confirm | Rejected by the owner (2026-10-15, answer 7). Confirm follows the control plane's appraisal of the new image's attestation. |
| More than one trial boot | Rejected by the owner (2026-10-15, answer 9). One trial; a failure means another OTA round. |
| Reusing an existing status for a bad OTA state | Rejected by the owner (2026-10-15, answer 10). `TFW_ERR_BUSY` (8) means a peripheral. |
| A W5x00 session in the AVR boot section | Measured: the 2560 is over by 660 to 1 230 bytes, and the 328P over by more than 4 KB (6.1). |
| The device verifies the CoRIM | The CoRIM is the verifier's artifact (owner, answer 3). The device verifies the SUIT envelope, which carries the same four facts. |
| EdDSA for the manifest | The ATECC608C verifies only P-256, and software Ed25519 needs 50 340 to 72 930 bytes. ES256 runs on every class with room. |
| An SPM entry point so the AVR application can write flash | It voids the boot section's read barrier from inside. |
| The 328P boot taking the backup itself | It does not fit: 4 504 or 4 292 bytes against 4 096 (6.3). The application takes it, and the boot only restores. |
| The element as the image store | Its data zone is about 1.2 KB against a 28 672-byte region. |
| A scratch-page swap on the ATmega2560 | With 256-page slots the scratch page wears out in about forty updates; see [architecture.md](architecture.md#alternatives-rejected). |

## Still PROPOSED: open questions

1. **The 328P (and 32U4) application budget.** The network path needs the 28 672
   region budget (6.3); the 16 384 policy line leaves no OTA path at all. This is
   the Ethernet ADR's option (a). The manifest on the 328P follows from it.
2. **The external store.** A SPI NOR of at least 512 Kbit on its own chip
   select, or the shield's microSD socket on D4, whose raw-block code is
   unmeasured. Which part, and which pin per board?
3. **The ATtiny85.** With no boot section it can have neither secure boot nor a
   verified update. Is it the one board without OTA?
4. **One key per board id for both the trailer and the manifest, or two?** One
   is proposed (decision 3).
5. **Manifest carriage.** The signed envelope appended to the `OTA_DATA` stream,
   counted in `OTA_BEGIN`'s total, with no new command (decision 3).
6. **The trial timeout.** The name `TFW_OTA_TRIAL_TIMEOUT_S` and each board's
   value.
7. **The 2560's slot size.** 64 KB slots matching the image budget (proposed:
   the shortest swap, and the running image under the 64 KB near-read boundary),
   or the largest pair the region holds (495 and 496 pages).
8. **The auth session's scope on TCP.** Ending with its connection as well as
   at reset (decision 8).
9. **The Cortex-M33 path**, with the trust-model ADR's open questions 1 and 6:
   - the RP2350 under TF-M and MCUboot, or its boot ROM's own A/B;
   - the non-secure application serving `0x68`–`0x6E` over PSA FWU as the device
     side of `device-ota`;
   - whether `tfw.ParseImage` also reads MCUboot's header.
10. **New EEPROM records.** The swap record at `0x0E0`, the `TRIAL`,
    `CONFIRMED` and `PENDING` values, and the failed measurement in the platform
    block's reserved range.
11. **The i.MX 95 M7**, with #15 (decision 12).

## Follow-ups (listed, not filed)

- AVR network OTA per part:
  - the 2560: the application stager and the boot's store read;
  - the 328P: the verify-and-copy boot and the application stager with backup;
  - the 32U4: measure, and wire an Ethernet controller.
- The ATmega2560 A/B by swap-move, with its EEPROM swap record.
- Manifest verification: the SUIT subset reader in the application, and the
  build's manifest emitter.
- The board id in the trailer on AVR: `tfw-boot`'s check, the build writing
  byte 6, and the bench reflash of every board.
- Transport selection per image: link files per board where only one transport
  fits (none today).
- The new status code `TFW_ERR_BAD_STATE`, with its wire vector and the Go
  mirror.
- OTA commands over Ethernet with the auth session: the allow-list widened to
  `0x61`, `0x62` and `0x68`–`0x6E`.
- The provisioning seam: `tfwprov` writes go-xkms's exported key into the
  element's slot; mirror go-xkms's row in the SPEC once it lands.
- The one trial: the watchdog arm in `tfw-boot`, and the deadline and the
  `tfw_app_after_update` call in the application SDK.
- `engineering.md`, `api.md` and `user.md` in this folder, with the code.
