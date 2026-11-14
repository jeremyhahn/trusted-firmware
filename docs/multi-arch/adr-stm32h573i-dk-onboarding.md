# ADR: onboarding the STM32H573I-DK

> **Status: PROPOSED** · Issue: [#126](../../../../issues/126)
> · SPEC row: `stm32h5` (future) · Related: #14
> ([`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md)),
> #15 ([`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md)),
> board page [`../boards/stm32h573i-dk.md`](../boards/stm32h573i-dk.md)
> · Scope: trusted-firmware, documentation only · Last verified: 2026-10-17
>
> Owner ruling, 2026-10-17 05:51Z, queue item 26: each arrived board gets one
> docs-only onboarding ADR. This is the STM32H573I-DK's. The port itself stays
> behind the AVR dev-complete gate (#7). This ADR changes no code, no `mk/` file,
> no board profile, no board id and no wire vector, and it decides nothing the
> owner has not ruled: every choice is an open question below.

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

## 1. What the board is for

The board serves the SPEC row **`stm32h5`** (future), as its **reference
board**: the Cortex-M33 target with ST's Secure Manager, hardware crypto and,
as the row states, a factory device certificate (not confirmed from any ST
document; board page). It enrols over Ethernet on board, so it needs no W5x00
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
| Secure Manager | ST's PSA-compatible secure world (secure boot, crypto, internal trusted storage, initial attestation, firmware update), whose access kit is export-controlled | UM3254, section to confirm; board page |

### The map onto DICE, as candidates only

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
| DeviceID and alias | the Secure Manager's initial attestation key, a key in our TF-M partition (ADR #15 (b1)), or a key in our own secure world (ADR #15 (a)) | open questions 2, 4 |

What does not change on any candidate: the device signs only the three fixed
structures (`docs/dice-attestation/architecture.md`), the alias certificate
carries `tcg-dice-UCCS-evidence` (DICE Attestation Architecture, Revision 23,
§6.1.4), and the DICE certificate profile and its verification are go-xkms's.

## 3. Where the board sits against ADR #14 and ADR #15

Both ADRs are ACCEPTED (owner ruling 632 item 208 a, 2026-11-14: the i.MX 95 M7
reference part, `mps2-an505` in CI, (b1) on the M33, MPU-only on the M7); this section restates what each says about this board
and decides nothing.

- **ADR #14** makes the i.MX 95's M7 the reference part and the STM32H5 the
  second target (decision 1). It expects tfw-boot on the STM32H5 after the QEMU
  board, from on-chip flash with our own layer 0, and leaves open whether QEMU
  `mps2-an505` lands with the STM32H5 port (its open question 9).
- **ADR #15** recommends, for the M33, TF-M Profile Medium with our attestation
  code as an Application RoT partition (b1). It names this board as TF-M's
  upstream platform, and it leaves open whether our partition sits on ST's
  Secure Manager or on upstream TF-M (its open question 2, which is the
  `stm32h5` row's open decision).
- **The tension between them.** Under (b1) MCUboot, not tfw-boot, boots this
  board, and ADR #14's tfw-boot expectation holds only if the owner keeps (a)
  here (ADR #15 open question 1 and follow-up F4). Under the Secure Manager the
  boot chain is ST's. This ADR adds no fourth option; open question 1 carries
  the choice.

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

None is decided here.

1. **The secure world on this board.** ST's Secure Manager, with its PSA token
   on go-xkms's EAT path; upstream TF-M with our partition (ADR #15 (b1)); or our
   own secure world (ADR #15 (a)), which is what ADR #14's tfw-boot expectation
   assumes? This is the `stm32h5` row's open decision and ADR #15's questions 1
   and 2.
2. **DICE layer 0.** Is the immutable layer ST's (STiRoT, or the Secure Manager's
   boot) or ours (an OEMiRoT, or tfw-boot under HDP)? It decides who measures
   the next layer and derives the CDI.
3. **The UDS.** Which secret is the UDS, who provisions it (ST at the factory, or
   our bench through SFI and `tfwprov`), and does HDP alone isolate it from the
   layers after layer 0?
4. **The factory device certificate.** The `stm32h5` row says the part carries
   one; no ST document read confirms it. If it exists, is it the DeviceID's
   endorsement, and does go-xkms have to accept ST's chain? That would be a gap
   filed against go-xkms, not worked here.
5. **The Secure Manager's licence and export control.** The access kit
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

- When the owner answers open question 1, update this ADR, ADR #14's decision 1
  table, ADR #15 follow-up F3, and the `stm32h5` row in one change.
- The first slice of the port reads the ST documents above at their current
  revisions and replaces every "to confirm" with a cited section.
