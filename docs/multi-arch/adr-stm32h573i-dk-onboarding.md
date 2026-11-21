# ADR: onboarding the STM32H573I-DK

> **Status: PROPOSED** · Issue: [#126](../../../../issues/126)
> · SPEC row: `stm32h5` (in progress) · Related: #14
> ([`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md)),
> #15 ([`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md)),
> board page [`../boards/stm32h573i-dk.md`](../boards/stm32h573i-dk.md)
> · Scope: trusted-firmware, documentation only · Last verified: 2026-11-21
>
> Owner ruling, 2026-10-17 05:51Z, queue item 26: each arrived board gets one
> docs-only onboarding ADR. This is the STM32H573I-DK's. The port itself stays
> behind the AVR dev-complete gate (#7). This ADR changes no code, no `mk/` file,
> no board profile, no board id and no wire vector, and it decides nothing the
> owner has not ruled: every choice is an open question below.

> **Ruling 935 (owner queue 236)** closes this ADR's open questions 1, 2 and
> 5 and the `stm32h5` row's open decision: the secure world on this board is
> upstream TF-M Profile Medium (ADR #15 (b1), ruling 1024, 2026-11-20, which
> replaced ruling 935's "our own TrustZone-M firmware") with our DICE layer,
> the DeviceID and the alias keys as an Application RoT partition, with
> evidence through go-xkms device-attest-01. ST's Secure Manager and a PSA
> token verifier are **not taken**; see
> [`adr-mcu-dice-attestation.md`](adr-mcu-dice-attestation.md) (its "Amended
> by ruling 1024" section). The hardware
> facts below stand; the passages that offered the Secure Manager are marked.

## The question

The STM32H573I-DK has arrived (CA m311). Before any port starts, the project
needs a record of:

1. what the board is for here, and which SPEC row it serves;
2. its root of trust, and how that maps onto TCG DICE;
3. where it sits against ADR #14 and ADR #15;
4. how it is brought onto the bench host without harming it;
5. what the owner still has to decide.

## Context

- **No ST document was read in this lane.** The board page records that every
  download from st.com failed on 2026-10-16, and this lane has no network fetch.
  Every ST document below is cited by its number and title. Each section is
  marked **to confirm**, and no section number is given that was not read.
  The documents are:
  - RM0481, the STM32H563/H573 reference manual (product state, option bytes,
    TrustZone, HDP, the boot paths);
  - DS14053, the STM32H573xx datasheet;
  - UM3143, *Discovery kit with STM32H573II MCU*;
  - UM3254, *Secure Manager for STM32H573xx microcontrollers* (Rev 8, August
    2025, per the board page);
  - UM3238, *STM32H573xx STM32TrusteeSM security guidance for SESIP 3
    certification*;
  - UM2237, *STM32CubeProgrammer software description* (the manual for
    `STM32_Programmer_CLI`; number to confirm).
- **The TCG DICE texts are not in `docs/reference/`.** Their licence forbids a
  copy, so [`../reference/README.md`](../reference/README.md) lists them by title
  and URL only ("Not redistributable"):
  - *TCG Hardware Requirements for a Device Identifier Composition Engine*,
    Revision 78, whose §5 form (2) is the layer-0 CDI we implement on AVR;
  - *TCG DICE Attestation Architecture*, Revision 23, whose §6.1.4 defines
    `tcg-dice-UCCS-evidence`.
- **What the board is** (board page): an STM32H573II, a Cortex-M33 with
  TrustZone-M, 2 MB of flash and 640 KB of RAM (neither confirmed against
  DS14053), an on-board STLINK-V3EC with a Virtual COM port, and 10/100 Ethernet
  from the MCU's MAC. It is an upstream TF-M platform at v2.3.1 (ADR #15).

## Requirements (audit, #460)

One line per requirement of SPEC row `stm32h5` (`docs/SPEC.md`) and of this
ADR, audited against main on 2026-11-16. "Test" names a file and a test in it;
`make host-test` runs the C suites and `make scripts-test` the Python ones. A
line with no test has none on main. Nothing below has run on either board.

| # | Requirement | Status | Code | Test |
|---|---|---|---|---|
| 1 | STM32H573I-DK as data on the generic Cortex-M port, board id 0x43 (#14, DS-2971) | done | `boards/stm32h573i-dk/board_profile.h`, `boards/stm32h573i-dk/board.c`, `boards/stm32h573i-dk/profile.mk` | `test/unit/test_board_stm32h573i-dk.c`: `test_the_board_is_its_registered_id` |
| 2 | The `stm32h5` SoC's clock: HSI at 64 MHz, flash wait states | done | `src/hal/cortex-m/stm32h5/hal_soc.c`, `src/hal/cortex-m/stm32h5/h5_calc.h` | `test/unit/test_stm32h5_soc.c`: `test_early_sets_flash_latency_3_for_64mhz`; `test/unit/test_stm32h5_calc.c`: `test_the_hsi_needs_three_wait_states` |
| 3 | The SoC's watchdog (IWDG) | done | `src/hal/cortex-m/stm32h5/hal_soc.c` | `test/unit/test_stm32h5_soc.c`: `test_arm_timeout_is_4_seconds`, `test_pet_writes_the_reload_key_only` |
| 4 | The SoC's GPIO | done | `src/hal/cortex-m/stm32h5/hal_gpio.c` | `test/unit/test_stm32h5_gpio.c`: `test_output_is_moder_01_no_pull_and_push_pull` |
| 5 | The ST-LINK console on USART1 (PA9/PA10) | done | `src/hal/cortex-m/stm32h5/hal_uart.c`, `src/hal/cortex-m/stm32h5/soc_console.h` | `test/unit/test_stm32h5_uart.c`: `test_the_pins_are_pa9_and_pa10_on_af7`, `test_brr_for_115200_at_64mhz` |
| 6 | Reset causes for the boot record | done | `src/hal/cortex-m/stm32h5/soc_reset.c` | `test/unit/test_stm32h5_reset.c`: `test_each_flag_with_a_value_maps_to_it` |
| 7 | Built in CI as `mps2-an505` is, application and tfw-boot | done | `.github/workflows/ci.yml` (steps `make firmware BOARD=stm32h573i-dk`, `make bootloader BOARD=stm32h573i-dk`); `mk/boot.mk` (row `stm32h573i-dk:soft:soft:p256-soft:0:0`) | the CI steps themselves; no QEMU machine models the STM32H5 (`profile.mk`) |
| 8 | tfw-boot programs the flash through a boot-only flash-interface driver, host-tested | done | `boot/hal/cortex-m/stm32h5/h5_flash.c`, `boot/hal/cortex-m/stm32h5/boot_flash.c` | `test/unit/boot/test_boot_stm32h5_flash.c`: `test_a_sector_in_bank_2_is_erased_through_bksel`; `scripts/test_stm32h5_boot_only_flash.py`: `BootFlashStaysBootOnly.test_no_stm32h5_application_links_boot_flash` |
| 9 | Peripherals of the default image beyond the row's text: EEPROM on bank 2, ADC1, PWM, tone, I2C4 (off by default) | done | `src/hal/cortex-m/stm32h5/hal_eeprom.c`, `ee_flash.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`, `hal_bus.c` | `test/unit/test_stm32h5_eeprom.c`: `test_the_user_sector_is_bank_2s_last`; `test/unit/test_stm32h5_adc.c`: `test_each_channel_is_one_conversion_of_its_12_bits`; `test/unit/test_stm32h5_pwm.c`: `test_duty_128_is_half`; `test/unit/test_stm32h5_tone.c`: `test_start_programs_the_divider_and_toggle_mode`; `test/unit/test_stm32h5_i2c.c`: `test_write_then_read_is_a_repeated_start` |
| 10 | NUCLEO-H563ZI as the secondary target (board id 0x47, console USART3) | partial: data and host tests; not cross-built by a CI step (only `LINT_ARCH_RUNS` in `mk/host.mk` analyses it) | `boards/nucleo-h563zi/board_profile.h`, `boards/nucleo-h563zi/board.c` | `test/unit/test_board_nucleo-h563zi.c`: `test_the_console_is_usart3_on_pd8_pd9`; `test/unit/test_stm32h5_uart_usart3.c`: `test_init_enables_irq60_only` |
| 11 | Onboarding: read-only factory record before any write; flash refused without it; restore writes the record back; no option-byte operation (section 4) | done (host-tested; never run) | `mk/arch/cortex-m.mk` (`board-record`, `flash`, `board-restore`), `boards/stm32h573i-dk/profile.mk` (`CM_FLASH_TOOL := stm32cubeprog`) | `scripts/test_cortex_m_flash.py`: `TestRecordWriteRestore.test_stm32h573i_dk`, `TestRefusals.test_write_without_record_is_refused`, `TestDryRun.test_no_refused_operation_in_any_line` |
| 12 | Runs on the STM32H573I-DK (console, `CAPS`, tfw-boot verify and boot) | not started (hardware) | none | none |
| 13 | Enrols over Ethernet through the MCU's MAC (no W5x00) | not started | none: no Ethernet MAC driver under `src/hal/cortex-m/stm32h5/` | none |
| 14 | Cortex-M33 TrustZone secure world: ADR #15 (b1), TF-M Profile Medium with our DICE partition, MCUboot as BL2 | not started (owner decision, open question 1) | none: the part runs with TrustZone off (`src/hal/cortex-m/stm32h5/soc_regs.h`) | none |
| 15 | Hardware crypto on the STM32H573 (PKA, HASH, RNG) | not started | none: the board's one tier is `soft` (`BOARD_TIERS` in `profile.mk`) | none |
| 16 | ST Secure Manager with its PSA attestation token verified on go-xkms's EAT path | not started (owner decision, open questions 1 and 5; go-xkms) | none | none |
| 17 | Factory device certificate as the DeviceID's endorsement | not started (open question 4; unconfirmed in any ST document; go-xkms) | none | none |
| 18 | NUCLEO-H563ZI proves our own secure world and software crypto on a part without ST's services | not started (the SPEC text predates ADR #15 (b1); see Follow-ups) | none | none |
| 19 | DICE layer 0 and UDS on the part (open questions 2, 3) | not started (owner decision) | none | none |
| 20 | Every "to confirm" replaced by a cited ST document section (open question 9) | not started (needs network access to st.com) | none | none |

Open question 8 is answered by row 1: the board took id `0x43` when the port
started.

### Remaining slices

1. Build the NUCLEO-H563ZI in CI: add `make firmware BOARD=nucleo-h563zi
   CRYPTO=soft` beside the STM32H573I-DK's steps in `.github/workflows/ci.yml`;
   the CI step itself proves it (a `.github` change, so a lane the coordinator
   briefs for it).
2. First run on the STM32H573I-DK: `make board-record`, then `make boot-flash`
   and `make flash`, and record `CAPS` (board 0x43, tier soft) and a tfw-boot
   refusal of a tampered image in `docs/boards/stm32h573i-dk.md`; needs the
   board, its ST-LINK on the bench and a device lane under ruling 188.
3. The same first run on the NUCLEO-H563ZI, recorded in
   `docs/boards/nucleo-h563zi.md`; needs hardware.
4. Replace every "to confirm" in this ADR and `docs/boards/stm32h573i-dk.md`
   with cited sections of RM0481, DS14053, UM3143, UM3254, UM3238 and UM2237;
   needs a lane with network access (open question 9), docs only.
5. An Ethernet MAC driver for the STM32H5 (a new
   `src/hal/cortex-m/stm32h5/hal_eth.c` with a host test
   `test/unit/test_stm32h5_eth.c` against the register model in
   `test/model/cortex-m/`), carrying the existing frames as the
   ethernet-transport row does; it needs an owner ruling first, since that row
   states no IP stack of ours runs on the device and the MCU's MAC, unlike a
   W5x00, has no hardware TCP/IP stack.
6. The M33 secure world on this board: upstream TF-M Profile Medium with our
   DICE partition, MCUboot as BL2, per ADR #15 (b1) and ruling 1024
   ([`adr-mcu-dice-attestation.md`](adr-mcu-dice-attestation.md), its "Amended
   by ruling 1024" section and slices (a) to (d)); not ST's Secure Manager.
7. DICE layer 0 and the UDS on the part (open question 3), then
   enrolment (device-attest-01) over Ethernet; blocked on slices 5 and 6.
8. Hardware crypto (PKA, HASH, RNG) as a tier beside `soft`, under the secure
   world slice 6 chooses; blocked on it.
9. The factory device certificate (open question 4): not the identity root
   under ruling 935, which derives the DeviceID in our secure world; whether
   the part carries one stays a hardware fact to confirm.
10. Align the SPEC row's NUCLEO-H563ZI clause ("our own TrustZone secure
    world") with ADR #15 (b1), or rule it a named exception for parts without
    a TF-M port; an owner or CA change to `docs/SPEC.md`, not a lane's.

## 1. What the board is for

The board serves the SPEC row **`stm32h5`** (future), as its **reference
board**: the Cortex-M33 target with hardware crypto. The row also names ST's
Secure Manager and a factory device certificate (the latter not confirmed
from any ST document; board page); under ruling 935 neither is the secure
world or the identity root
([`adr-mcu-dice-attestation.md`](adr-mcu-dice-attestation.md)). It enrols over Ethernet on board, so it needs no W5x00
module. Its sibling target in the same row, the NUCLEO-H563ZI, proves a secure
world of our own on a part without ST's secure services; that board is not
onboarded by this ADR. Through ADR #14 the board also serves the `cortex-m`
row, as its second target and first M33.

## 2. Root of trust, and the map onto TCG DICE

### What the part provides (from ST's documents, all to confirm)

| Facility | What it is | Source |
|---|---|---|
| Immutable root of trust | ST's boot ROM. ST names an ST-provided immutable root of trust (STiRoT) and an OEM-provided one (OEMiRoT) as the two secure boot paths of the H573 | RM0481, section to confirm; UM3254, section to confirm |
| TrustZone-M | Armv8-M Security Extension: SAU/IDAU, with the flash and SRAM security attribution set by the secure world (ADR #14 decision 1 names MPCBB and TZSC) | RM0481, section to confirm |
| Product state | the part's lifecycle, held in option bytes, which sets what debug and flash access remain. Its states, which transitions are allowed, and which of them erase or are irreversible, are **to confirm** from RM0481 | RM0481, section to confirm |
| HDP | the hide-protection area: boot code that hides itself from later code before handing over, so later stages cannot read it. Its levels and how they advance are **to confirm** | RM0481, section to confirm |
| Secure firmware install (SFI) | ST's encrypted install of firmware and option bytes into a part in the provisioning state, driven by STM32CubeProgrammer | RM0481 and UM2237, sections to confirm; the SFI application note's number is to confirm |
| Secure Manager | ST's PSA-compatible secure world (secure boot, crypto, internal trusted storage, initial attestation, firmware update), whose access kit is export-controlled. **Not taken** as our secure world (ruling 935, [`adr-mcu-dice-attestation.md`](adr-mcu-dice-attestation.md)) | UM3254, section to confirm; board page |

### The map onto DICE, as candidates only

Ruling 935 settles the rows that named the Secure Manager: the DICE layer,
the measurement of the next layer and the DeviceID and alias keys are in our
DICE partition under TF-M (ruling 1024;
[`adr-mcu-dice-attestation.md`](adr-mcu-dice-attestation.md), its "Amended
by ruling 1024" section). The table is
kept as the candidates first recorded.

TCG's DICE hardware requirements (Revision 78) ask for a Unique Device Secret
readable only by the DICE engine, and for a first-layer CDI derived from that
secret and a measurement of the next layer (§5, form (2) of which is ours on
AVR). On this part each element has more than one candidate:

| DICE element | Candidates on the STM32H573 | Bears on |
|---|---|---|
| DICE engine (immutable) | the boot ROM's STiRoT path, or an OEMiRoT we write, installed in a hidden area | open questions 1, 2 |
| UDS | a hardware unique key reachable only by secure code before HDP closes (whether the H573 has one usable this way is to confirm in RM0481), or a secret we provision through SFI | open question 3 |
| Measurement of the next layer | taken by STiRoT, by the Secure Manager, or by our layer 0 | open question 2 |
| Isolation of the CDI from later layers | HDP, the TrustZone boundary, or both | open question 3 |
| DeviceID and alias | the Secure Manager's initial attestation key, or a key in our TF-M partition (ADR #15 (b1), accepted) | open questions 2, 4 |

What does not change on any candidate: the device signs only the three fixed
structures (`docs/dice-attestation/architecture.md`), the alias certificate
carries `tcg-dice-UCCS-evidence` (DICE Attestation Architecture, Revision 23,
§6.1.4), and the DICE certificate profile and its verification are go-xkms's.

## 3. Where the board sits against ADR #14 and ADR #15

Both ADRs are ACCEPTED (owner ruling 632 item 208 a, 2026-11-14: the i.MX 95 M7
reference part, `mps2-an505` in CI, (b1) on the M33, MPU-only on the M7); this section restates what each says about this board
and decides nothing.

- **ADR #14** makes the i.MX 95's M7 the reference part and the STM32H5 the
  second target (decision 1). Its HAL table leaves the boot stage on the M33
  boards to ADR #15's model, and the ruling picks QEMU `mps2-an505` as the CI
  machine.
- **ADR #15** decides, for the M33, (b1): TF-M Profile Medium with our DICE
  attestation code as an Application RoT partition in the secure world, MCUboot
  as BL2. It names this board as TF-M's upstream platform, and it leaves open
  whether our partition sits on ST's Secure Manager or on upstream TF-M (its
  open question 2, which is the `stm32h5` row's open decision). Our own secure
  world, ADR #15 (a), is held in reserve for M33 parts TF-M has no port for; it
  is not an option on this board.
- **Ruling 935** (after both ADRs) names our own TrustZone-M firmware as the
  secure world on the STM32H5, not ST's Secure Manager
  ([`adr-mcu-dice-attestation.md`](adr-mcu-dice-attestation.md)), and ruling
  1024 (2026-11-20) then replaced "our own TrustZone-M firmware" with (b1)
  (that ADR's "Amended by ruling 1024" section); so on this board (b1) stands
  and only the Secure Manager option is replaced.
- **Before ruling 935**: Under (b1) MCUboot, not tfw-boot, boots this board
  (ADR #15: "Under (b1), tfw-boot runs on neither bench board"), so ADR #14's
  HAL table and ADR #15 agree. The Secure Manager option, under which the boot
  chain would have been ST's, is not taken.

## 4. Onboarding on the bench host

These steps record the board's factory state and nothing more. No step writes
the part.

1. **Cabling.** The owner cables the board. A request goes to the owner through
   the CA, never directly: the STLINK-V3EC USB connector to the bench host for debug
   and the Virtual COM port, and, when a later slice needs it, the Ethernet port
   to the bench network.
2. **One device per lane.** One lane holds this board at a time, and the lane
   launch names its device. No other lane opens its STLINK while it does.
3. **The factory record, read-out only.** With ST's own tool,
   `STM32_Programmer_CLI` (UM2237), the lane connects and reads:
   - the option bytes and the product state;
   - the device and revision identifiers;
   - a read-back of every flash region the product state lets the debugger
     read, for the restore below.

   Nothing is written, erased, or unlocked. Regions the product state hides
   (secure flash, HDP areas) are recorded as hidden, not forced. The exact
   commands are taken from UM2237 at its current revision, which a port's first
   slice reads.
4. **Where the record lives.** On the bench, outside this repository. The repo
   carries no bench serial number, no MAC address, no IP address and no host
   name; the board page carries only vendor facts.
5. **Restore.** The record exists so that the board can be returned to its
   factory image with ST's own tools. A restore writes only what the record
   read, and only after the owner rules how (open question 6).
6. **Never a regression.** No lane changes the product state or the option
   bytes, in either direction, without an owner ruling for that board. A product
   state transition can erase the part or be irreversible (RM0481, to confirm),
   so a lane that finds itself needing one stops and reports.

## Open questions for the owner

None is decided here. Owner ruling 632 item 208 a (2026-11-14) accepted ADRs
#14 and #15: the M33 secure world is TF-M Profile Medium with our DICE
attestation partition (ADR #15 (b1)), so our own secure world (ADR #15 (a)) is
no longer offered for this board.

1. **Closed by rulings 935 and 1024: which secure world.** TF-M Profile
   Medium with our DICE partition; not ST's Secure Manager, no PSA token
   ([`adr-mcu-dice-attestation.md`](adr-mcu-dice-attestation.md), its
   "Amended by ruling 1024" section).
2. **Closed by ruling 935: DICE layer 0.** Ours, in our secure world, which
   measures the next layer and derives the CDI; not the Secure Manager's.
3. **The UDS.** Which secret is the UDS, who provisions it (ST at the factory, or
   our bench through SFI and `tfwprov`), and does HDP alone isolate it from the
   layers after layer 0?
4. **The factory device certificate.** The `stm32h5` row says the part carries
   one; no ST document read confirms it. If it exists, is it the DeviceID's
   endorsement, and does go-xkms have to accept ST's chain? That would be a gap
   filed against go-xkms, not worked here.
5. **Moot under ruling 935: the Secure Manager's licence and export
   control.** The Secure Manager is not taken. As first asked: the access kit
   X-CUBE-SEC-M-H5 is export-controlled. May the project take it, and do its
   non-secure interface libraries pass the field-of-use check against
   AGPL-3.0-or-later (ADR #15 open question 2)?
6. **Restore and product state.** May a lane ever restore the factory image, or
   does only the owner? Is any product state transition, including the one SFI
   needs, allowed on this board, and on whose ruling?
7. **The TCG DICE text.** The DICE specifications are listed in
   `docs/reference/` by title and URL only. Is that enough for a port that
   implements them, or must the revision a port reads be pinned elsewhere?
8. **Board id.** The board takes an id in the Cortex-M range `0x40`–`0x7F`
   when the port starts. Allocated then, or now?
9. **The ST documents.** RM0481, DS14053, UM3143, UM3254, UM3238 and UM2237 could
   not be read. Should a lane with network access fetch them and replace every
   "to confirm" here and on the board page before the port starts?

## Follow-ups (proposed, not filed)

- The `stm32h5` row in `docs/SPEC.md` still reads "our own TrustZone secure
  world"; aligning it with ADR #15 (b1) is a SPEC change for the CA, not this
  ADR's.
- When the owner answers open question 1, update this ADR and ADR #15 follow-up
  F3 in one change.
- The first slice of the port reads the ST documents above at their current
  revisions and replaces every "to confirm" with a cited section.
