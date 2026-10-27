# OTA

> **Status: DRAFT** · SPEC row: `ota` (in progress) · Issue:
> [#52](../../../../issues/52) · Last verified: 2026-10-27

Signed over-the-air updates of the whole image, firmware and application
together: A/B where the flash holds two slots, one internal slot with its backup
in an external store where it does not, one trial boot that rolls back by itself
unless the control plane confirms it, and an anti-rollback floor. This page is
the row's design in one place. It decides nothing. Every decision it cites is in
the [ADR](adr-ota-image-format-and-trust.md) or [architecture.md](architecture.md),
and the code that meets each requirement is listed in
[engineering.md](engineering.md), "The row on main".

| Document | What it holds |
|---|---|
| [`adr-ota-image-format-and-trust.md`](adr-ota-image-format-and-trust.md) | Decisions 1 to 13: image format, trailer v1 with the board id, the SUIT-subset manifest, what boot verifies, anti-rollback, transports, slot layout, wire commands, the state machine, keys, frameworks first, the i.MX 95 M7, the `sdk/go` helper; the seams; the open questions |
| [`architecture.md`](architecture.md) | Ownership, data flow, the arch classes, the device state machine per part, the manifest gate, the stager, tfw-boot's copy, the 328P and 32U4 fallback, the round under simavr |
| [`engineering.md`](engineering.md) | Each requirement met or unmet on main, with its file; files, gating by object list, tests, sizes |
| [`api.md`](api.md) | The wire commands `0x68` to `0x6F`, the row key's authorization, the ESP32 session, `sdk/go` |
| [`user.md`](user.md) | Seal, stage, activate, and what the next reset does |

## Standards

Implemented precisely, nothing designed in their place (handbook
`documentation.md` §9). Each is on file in [`../reference/`](../reference/README.md).

| Part | Standard | On file |
|---|---|---|
| The manifest | SUIT manifest, the subset in ADR decision 3 | `draft-ietf-suit-manifest-37.txt` (pinned -37) |
| The manifest's algorithms | `suit-sha256-es256-ecdh-a128ctr`, authentication half | `draft-ietf-suit-mti-23.txt` (pinned -23) |
| The manifest's and the authorization's signature | `COSE_Sign1`, detached payload; ES256; SHA-256 identifier | `rfc9052.txt`, `rfc9053.txt`, `rfc9054.txt` |
| The manifest's encoding | CBOR | `rfc8949.txt` |
| The image measurement | SHA-256 | `NIST.FIPS.180-4.pdf` |
| The rollback counter | `suit-manifest-sequence-number` (§8.4.2), equal to the trailer's `image_version`, the one security version boot compares (ADR decision 5) | `draft-ietf-suit-manifest-37.txt` |
| The measurement the control plane appraises before it confirms | TCG DICE, Layer 0 CDI over H(code) | listed, not copied (TCG licence) |

No standard covers the image container of a raw AVR flash image, so trailer v1
(ADR decision 2) is ours. Where a platform has a framework, the framework's own
format is composed instead (ADR decision 11): MCUboot's on the Cortex-M33 under
TF-M, ESP-IDF's app image on the ESP32-P4, AHAB's container on the i.MX 95 M7.
The CoRIM is not ours: go-xkms encodes and signs it and appraises `FWHash`
against it, and go-quicbaas stores it. The device never parses one.

## Slot layout per board class

From each `board_profile.h` (`TFW_FLASH_TOTAL`, `TFW_FLASH_BUDGET`) and ADR
decision 7.

| Board | Flash | Layout | State on main |
|---|---|---|---|
| ATtiny85 | 8 KB, no boot section | none: no secure boot, so no verified update | proposed (ADR open question 3) |
| ATmega328P | 32 KB, 4 KB boot, 16 384 B application budget | one internal slot; the previous image (`PREV`) is backed up to the external store and restored by tfw-boot, which evaluates it again before it runs | written, not run: the restore overflows the boot section by 1 886 B ([architecture.md](architecture.md#the-328p-and-32u4-fallback-from-prev-52)) |
| ATmega32U4 | 32 KB, 4 KB boot, 14 336 B budget | as the 328P | written, not run: 1 870 B over; no Ethernet controller wired |
| ATmega2560 | 256 KB, 8 KB boot | A/B by swap-move: 64 KB primary at `0x00000`, secondary at `0x10100` (`TFW_SWAP_SLOT_LEN`), progress journalled at EEPROM `0x0E0`–`0x0E9` | runs on the `BOOT_OTA=1` and `BOOT_STORE=1` rows ([architecture.md](architecture.md#atmega2560-ab-by-swap-move)) |
| ESP32-P4, esp32-qemu | 16 MB | ESP-IDF's `ota_0`/`ota_1`, `otadata` and app rollback, composed | runs (#48) |
| Cortex-M33 under TF-M | per part | MCUboot's primary and secondary, written through PSA Firmware Update, composed | not started (ADR open question 9) |
| Cortex-M on tfw-boot (mps2-an505) | per part | A/B: the part's bank swap, swap-move otherwise; geometry is boot-HAL data | runs under QEMU (`sdk/go/cmd/tfwctl/ota_ab_qemu_test.go`, CI) |
| i.MX 95 M7 (frdm-imx95-m7) | 256 KiB ITCM | AHAB, pending #15 (ADR decision 12) | not started |

## The update sequence

Over the commands on main (`src/core/commands.def`, [api.md](api.md)):

1. go-trusted-platform reads `APP_INFO` (`0x06`) and `OTA_INFO` (`0x6C`).
2. It streams the sealed image with `OTA_BEGIN` (`0x68`, the total length),
   `OTA_DATA` (`0x69`) and `OTA_END` (`0x6A`), polling `OTA_STATUS` (`0x6B`).
   The stream is the image, a little-endian u16 length and the SUIT envelope,
   then trailer v1 (ruling 109; ADR decision 3). On AVR the application stages
   it in the external store (#75); this leg also runs over TCP.
3. It sends `OTA_AUTH` (`0x6F`), the row key's 64-byte ES256 signature over
   the action, the measurement and the version, then `OTA_ACTIVATE` (`0x6D`).
   On the ATmega2560's element tier the application checks the manifest there,
   before it writes the authorization (ruling 121). Serial only today.
4. At the next reset tfw-boot checks the activate authorization, the trailer's
   board id, the image signature and the floor, copies or swaps the image in,
   arms the watchdog and boots it once on trial. The application calls
   `tfw_app_after_update`.
5. The control plane attests the device within the trial timeout
   (`TFW_OTA_TRIAL_TIMEOUT_S`, 60 s on AVR) and appraises `FWHash` through
   go-quicbaas. If the appraisal passes it sends `OTA_AUTH` and `OTA_CONFIRM`
   (`0x6E`). The application writes the confirm request, 75 bytes at EEPROM
   `0x0EA`–`0x134` on the 2560.
6. At the next reset tfw-boot verifies the confirm request's signature (#231),
   advances the floor to the image's version and settles `GOOD`.

## When rollback happens

ADR decision 9; [architecture.md](architecture.md), "Device state machine".

- A reset in `TRIAL` without a confirm request whose signature verifies: the
  watchdog, the per-board deadline, a crash, or a power cut.
- `init` fails, or `tfw_app_after_update` refuses, during the trial. Either
  ends the trial at once by the same reset.
- An image refused after the swap: board id, signature or floor.

A rolled-back image is never copied back in, and its `MARK` is cleared (item 56).
Recovery is another OTA round. An activation signed for another measurement, or
an image below the floor, is not taken, so nothing rolls back.

## Where the anti-rollback floor lives

ADR decision 5. It advances only at settle, never on a trial boot.

| Tier | Counter |
|---|---|
| AVR element (ATECC608C), every shipped AVR board | the element's monotonic counter 0, `TFW_ATECC_COUNTER_BOOT` |
| AVR soft | `rollback_floor`, 4 bytes at platform-block offset 40 (`0x028`–`0x02B`), inside tfw-boot's `[0x000, 0x140)` ([../secure-boot/api.md](../secure-boot/api.md#reserved-eeprom)); it holds against a replayed image, not against code on the device |
| ESP32-P4 | the eFuse secure version, through ESP-IDF's anti-rollback |
| Cortex-M33 under TF-M | MCUboot's security counter in TF-M's non-volatile counters |

No new EEPROM byte is needed. Every OTA record (`boot_state` at offset 48, the
swap journal, the confirm request) is already inside `[0x000, 0x140)` (#125),
and `0x135`–`0x13F` is spare.

## Firmware and application together

One image holds both. An application repository links one application into
one image, with no dynamic loading (row `application-sdk`). That gives one
trailer, one measurement, one security version, one floor and one trial. The
version for people is the application descriptor's, which `APP_INFO` reports.
The application's hooks are `before_update` before every installing reset and
`after_update` on the trial boot. Neither confirms anything.

## Seams

This project keeps no registry, store or update server (CLAUDE.md L1, L2). The
requirements on each repo are in the ADR, "Seams".

| Owner | Seam | Issue |
|---|---|---|
| go-trusted-platform | `device-ota`: the device-facing endpoint delivers the image and sends `OTA_AUTH`, `OTA_ACTIVATE` and `OTA_CONFIRM` through `sdk/go`; control-plane confirm after attestation | to file |
| go-quicbaas | `firmware-reference-values`: each image, its SUIT envelope and its signed CoRIM, keyed by board id, application and security version | to file |
| go-xkms | the image key and manifest signer, one per board id; signing the authorization; the provisioning row that places the verification key in the element | to file |
| qrdb | `ota-permissions`: `ota.activate` and `ota.confirm`, tenant-scoped | to file |

## Requirements for the row's later slices

One PR per row (m751). Items 3, 7, 8 and 11 are done on main, each with where;
every other item is unmet on main ([engineering.md](engineering.md)).

1. The ATmega328P restore from `PREV` fits its boot section and runs under simavr, confirm included (after question 1). On the Q1 recommendation (a) (Software, #257), pending the Company Agent's ruling, the 328P ships OTA with the trailer signature check and the restore from `PREV` and without the manifest check. That recommendation does not make the restore fit: tfw-boot links no manifest check on any AVR row, and the 328P application already links `ota_manifest_none.c` (`OTA_MANIFEST` is set only in the 2560's `profile.mk`). The boot stays 1 886 B over, and it reads no confirm request on this row ([architecture.md](architecture.md#the-328p-and-32u4-fallback-from-prev-52)).
2. The ATmega32U4: the same, with an Ethernet controller wired and the stager measured.
3. The ATmega2560 application's size with the manifest check, measured before and after on the CI image. Done: measured at f2e8d4e3 ("Measurements" below; [engineering.md](engineering.md), "The row on main").
4. The manifest check on the 328P and 32U4, if their budget ruling holds it. On the Q1 recommendation (a), pending the Company Agent's ruling: not on these parts.
5. The manifest check on the soft tier, with a home for its verification key (question 5).
6. `OTA_AUTH`, `OTA_ACTIVATE` and `OTA_CONFIRM` over TCP, the authorization ending with its connection (question 4). The scope's docs and host tests are done (`test/unit/test_ota_hold_link.c`, `TestE2EOTAAuthSessionEndsWithItsConnection`); admitting the three over TCP is still with the owner (item 48).
7. The ATmega2560 row with both OTA in boot and the store, or link files that pick one update path per image (question 6). Done: one update path per image, decided 2026-10-26, and `make bootloader` refuses a 2560 build with both ([architecture.md](architecture.md), "The 2560 with OTA and the store").
8. The stale `MARK` after a UART update, closed before any row links both a boot session and the store (question 7). Done: closed by one update path per image (question 6): no row links both. A future row that does takes option 3, the boot taking a staged image only when the platform block says the application staged one.
9. Every bench board reflashed with an image whose trailer carries its board id.
10. The provisioning seam: `tfwprov` writes go-xkms's exported key into the element's slot, and the SPEC mirrors go-xkms's row.
11. The tfw-boot A/B on the generic Cortex-M port (mps2-an505), its slot geometry as boot-HAL data. Done: `boot/boot_swap.c` under QEMU, the geometry in `boot/hal/cortex-m/boot_slots.h` ([engineering.md](engineering.md), "The row on main").
12. The Cortex-M33 under TF-M: `0x68`–`0x6E` mapped onto PSA Firmware Update in the non-secure application (question 8).
13. The i.MX 95 M7 under AHAB (with #15).
14. The ESP32-P4 eFuse anti-rollback on the board (question 9).

## Measurements

The ATmega2560 application with and without the manifest check (requirement
3), measured at f2e8d4e3. The build is CI's row, `make firmware BOARD=atmega2560
CRYPTO=atecc608a` (the Ethernet image, W5100, stager on), read by `make
firmware`'s own size check. The toolchain is avr-gcc 14.2.0 (Debian gcc-avr
1:14.2.0-2, binutils-avr 2.43.50.20250108-1, avr-libc 1:2.2.1-1), unpacked as
in [wiznet-probe.md](../development/wiznet-probe.md), "Toolchain".

| Image | Flash | RAM |
|---|---|---|
| With the manifest check (`OTA_MANIFEST := 1`, the profile's: `src/core/ota_manifest.c`, `src/core/suit.c`) | 28 802 / 65 536 | 1 210 / 2 048 |
| Without (`OTA_MANIFEST=0`: `src/core/ota_manifest_none.c`) | 25 030 / 65 536 | 1 192 / 2 048 |
| The check's cost | 3 772 | 18 |

The ADR's estimate was about 3 086 bytes: 2 886 for the reader, linked against
stubs, plus up to 200 for the check.

## Open questions for the owner

Each is already open in the docs named. This page only gathers them.

1. The 328P and 32U4 application budget, 28 672 against the 16 384 policy line, and how their restore fits a 4 KB boot section (ADR open question 1; [architecture.md](architecture.md), "The store on the 4 KB parts").
2. The external store's select: decided 2026-10-26 under ruling 109 (#52 comment 152690), D8 on every AVR board (ADR open question 2; architecture.md, "The selects").
3. The ATtiny85: decided 2026-10-26 under ruling 109 (#52 comment 152690), the one board without OTA (ADR open question 3).
4. The authorization's scope on TCP: decided 2026-10-26 under ruling 109 (Software, #257), it ends with its connection as well as at reset; docs and host tests done. Whether the ATmega2560's activate and confirm move to TCP is still with the owner (item 48) (ADR open question 8; [engineering.md](engineering.md), the TCP open question).
5. One key per board id for both the trailer and the manifest, and whether the soft tier gets a stored key to check the manifest (ADR open question 4; engineering.md).
6. The 2560 with OTA and the store: decided 2026-10-26, one update path per image, which the build now enforces (architecture.md, "The 2560 with OTA and the store").
7. The stale `MARK` after a UART update: decided 2026-10-26 under ruling 109 (#52 comment 152690). One update path per image (question 6) closes it today; a future row with both a boot session and the store takes option 3 (architecture.md, "A stale mark after a UART update").
8. The Cortex-M33 path: the RP2350 under TF-M or its boot ROM's A/B; PSA FWU as the device side of `device-ota`; whether `tfw.ParseImage` reads MCUboot's header (ADR open question 9).
9. Which releases advance a board's eFuse floor on the ESP32-P4 ([api.md](api.md), "ESP32").
10. What had shipped while still marked PROPOSED in the ADR: decided 2026-10-26. The 2560's 64 KB slots under ruling 109 (Software, #257; open question 7), and the EEPROM records and bound, which #125 settled at `0x140` (#52 comment 152690; open questions 10 and 12).
