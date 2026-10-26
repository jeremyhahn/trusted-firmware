# ADR: onboarding the Raspberry Pi Pico 2 W (RP2350)

> **Status: PROPOSED** · Issue: #128 (owner queue item 26) · SPEC rows: `rp2350`
> (future), `cortex-m` (future) · Related: ADR #14
> ([`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md)),
> ADR #15 ([`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md)),
> the board page [`../boards/rp2350-pico2w.md`](../boards/rp2350-pico2w.md) ·
> Scope: trusted-firmware only · Last verified: 2026-10-17
>
> Documentation only. No code, build file, board profile, board id or SPEC status
> changes with this ADR. The owner ruled (2026-10-17 05:51Z, queue item 26) that
> each arrived board gets one docs-only onboarding ADR now, and that the port
> itself stays behind the AVR dev-complete gate (#7). Nothing here is decided until
> the owner accepts it; every choice is an open question below.

Documents cited:

- **[PB]** *RP2350* product brief, Raspberry Pi Ltd, RP-008374-DS-1, August 2024.
  Read; cited by the section names the board page uses.
- **[RP-DS]** *RP2350 Datasheet: A microcontroller by Raspberry Pi*, Raspberry Pi
  Ltd, RP-008373-DS. **Not read in this lane**: revision and every section are
  **to confirm**. Its security, boot ROM and OTP chapters are the register-level
  source for everything in "Root of trust" below.
- **[DS]** *Raspberry Pi Pico 2 W Datasheet*, RP-008304-DS (file RP-008304-DS-2);
  only its section headings were read (see the board page).
- **[DICE-HW]** TCG *Hardware Requirements for a Device Identifier Composition
  Engine*, Revision 78, listed in [`../reference/README.md`](../reference/README.md)
  "Not redistributable" (title and URL only).
- **[DICE-AA]** TCG *DICE Attestation Architecture*, Revision 23, listed in the
  same table.

## The question

The Pico 2 W has arrived on the bench (CA m311). Before any port starts, the
project records what the board is for, what its root of trust is and how that
maps onto TCG DICE, where it sits against ADRs #14 and #15, and how it is brought
up on the bench host without changing the device irreversibly.

## Context

- **The SPEC row.** `rp2350` (future): "the firmware core as a HAL port, with
  DICE evidence on the part's OTP and secure boot. Enrollment goes through
  go-xkms into go-trusted-platform." The row's design folder is
  `docs/multi-arch/`, so this ADR lives there. The board also counts as an M33
  target under the `cortex-m` row.
- **The board** ([`rp2350-pico2w.md`](../boards/rp2350-pico2w.md)): an RP2350
  with dual Cortex-M33 or dual Hazard3 RISC-V cores, selectable, 520 KB of SRAM,
  no on-chip flash and 4 MB of external QSPI flash, an SWD connector and no
  on-board probe, a Micro-USB port to the RP2350's USB controller, and **no
  Ethernet**: Wi-Fi and Bluetooth through a CYW43439 on SPI.
- **The gate.** The `cortex-m` row, and with it any RP2350 port, waits on the
  AVR physical end-to-end run (#7). This ADR does not lift it.
- **Ownership** (CLAUDE.md, L1/L2). This project owns the firmware and on-device
  key handling. DICE ASN.1, certificate profiles and attestation verification are
  go-xkms's; enrolment and the device registry are go-trusted-platform's. The
  RP2350 boot ROM and its secure-boot format are Raspberry Pi's, composed and
  never re-implemented.

## What the board is for

The Pico 2 W serves the SPEC row **`rp2350`**, and is the cheapest physical
Cortex-M33 with TrustZone-M on the bench. In this project it is:

1. a device that runs the firmware core as a HAL port and speaks the AVR wire
   protocol unchanged;
2. a DICE device whose evidence is rooted in the RP2350's OTP and boot-ROM secure
   boot rather than in an external element;
3. a physical check, under the `cortex-m` row, of the M33 trust model ADR #15
   recommends, on a part upstream TF-M already supports (v2.3.1).

It is not a reference part (that is the i.MX 95's M7, ADR #14 decision 1), and it
is not the first M33 (that is the STM32H5, same decision).

## Root of trust

From [PB] ("Overview", "Key features"); the register-level detail is in [RP-DS],
sections **to confirm**.

| Facility | What [PB] states | Where in [RP-DS] |
|---|---|---|
| Boot ROM secure boot | optional boot signing, enforced by the on-chip mask ROM, with the key fingerprint in OTP | boot ROM chapter, secure boot section: **to confirm** |
| OTP key storage | 8 KB of antifuse OTP for key storage, with protected storage for an optional boot decryption key | OTP chapter: **to confirm** |
| OTP lock | not described in [PB]; per-page read/write locking is expected to be in [RP-DS] | OTP chapter, page locks: **to confirm** |
| TrustZone-M | the security architecture is "built around Arm TrustZone for Cortex-M"; peripherals, GPIOs and DMA channels individually assignable to security domains | security / bus-access chapter: **to confirm** |
| Glitch detectors | fast glitch detectors and hardware mitigations for fault-injection attacks | security chapter, glitch detector section: **to confirm** |
| Crypto | hardware SHA-256 and a TRNG; no public-key accelerator named | SHA-256 and TRNG chapters: **to confirm** |

### How it maps onto TCG DICE

DICE [DICE-HW] asks for a Unique Device Secret (UDS) that only the DICE engine
can read, latched unreadable before the first mutable code runs, and a layer 0
that measures the next layer and derives its CDI. This repository implements
form (2) of [DICE-HW] §5, CDI = HMAC(UDS, H(code)) (`boot/boot_cdi.h`), and
emits `tcg-dice-UCCS-evidence` per [DICE-AA] §6.1.4.

The candidate mapping, for the owner to accept or change (open questions 2-4):

| DICE element | RP2350 candidate |
|---|---|
| UDS | a secret in an OTP page, locked against reads from non-secure code and from later boot stages |
| Latch | the OTP page lock applied by layer 0 before it hands off |
| DICE engine / layer 0 | the boot ROM verifies the first image's signature against the OTP key fingerprint; whether the ROM itself derives a CDI is **to confirm** in [RP-DS]. If it does not, layer 0 is the first verified image (tfw-boot, or TF-M's BL1/BL2 under ADR #15's (b1)), which reads the UDS, derives the CDI and locks the page |
| Measurement | SHA-256 of the next image, on the hardware SHA-256 |
| DeviceID / alias keys | P-256 in software from the CDI, isolated in the TrustZone secure world; the M33 can derive a per-boot alias key, which the ATECC608 cannot (ADR #15, "Context") |
| Fault-injection resistance | the glitch detectors, configuration **to confirm** |

Nothing in this mapping is enabled during onboarding: no OTP is written and
secure boot stays off (see "Onboarding on the bench host").

## Against ADR #14 and ADR #15

- **ADR #14** (PROPOSED). The RP2350 is an M33 board that, per decision 2, would
  reuse `src/hal/cortex-m/` and add `src/hal/cortex-m/rp2350/` with
  `CPU := cortex-m33`. Its CI stand-in would be the M33 target QEMU `mps2-an505`,
  which lands with the STM32H5 or with #15; no RP2350-specific QEMU machine is
  known to this ADR (to confirm). The Hazard3 RISC-V cores are outside ADR #14,
  which covers Arm only. Board id: none allocated in the Cortex-M range
  `0x40`-`0x7F`.
- **ADR #15** (PROPOSED). The RP2350 is named among the M33 targets with an
  upstream TF-M port. Under its recommendation (b1) our attestation code would be
  an Application RoT partition on TF-M Profile Medium, with MCUboot as BL2. The
  RP2350 bears on its open question 4 (an OTP-plus-TrustZone key is neither the
  element tier nor an SoC enclave) and question 5 (a per-boot alias key from a
  CDI). This ADR adopts neither ADR; it follows whatever the owner rules there.

## Onboarding on the bench host

1. **Cabling** is the owner's: the request goes to the owner through the CA,
   naming the USB link and, if wanted, an SWD probe. No lane plugs or moves a
   cable.
2. **Factory record** before anything is flashed, with the vendor's own tool:
   `picotool save --all` in BOOTSEL mode, a read-out of the whole flash to a file.
   Read-out only; the record is kept off the repository.
3. **Restore** is the factory record written back to flash with `picotool`.
   Flash only.
4. **One device per lane.** A lane holds this board alone, and is launched with
   its device passed through.
5. **Never an OTP write, and never secure boot enabled.** OTP is antifuse and
   one-way, and enabling secure boot is an OTP write. Both stay out of every
   lane until the owner rules (open question 1).
6. **Nothing identifying in the repository**: no bench serial numbers, MACs, IP
   addresses or host names in code, docs, logs or commits.

## Open questions for the owner

None of these is decided here.

1. **OTP and secure boot.** Is any OTP write, and secure boot in particular, ever
   allowed on this bench board, or only on a second, sacrificial one?
2. **Layer 0.** Is layer 0 the boot ROM plus tfw-boot, or the boot ROM plus TF-M
   BL1/BL2 under ADR #15's (b1)?
3. **UDS.** Which OTP page holds the UDS, who provisions it (the bench through
   `tfwprov`, or on-device from the TRNG), and when the lock is applied.
4. **Crypto tier.** Which `CAPS` crypto tier value an OTP-plus-TrustZone key
   reports (ADR #15 open question 4).
5. **Transport.** The board has no Ethernet. Does it enrol over Wi-Fi (CYW43439),
   USB, a UART, or an added W5x00 module (#55)?
6. **Cores.** Arm Cortex-M33 only, or the Hazard3 RISC-V cores too (outside ADR
   #14)?
7. **CI stand-in.** Is QEMU `mps2-an505` enough for this board, or is an RP2350
   emulator wanted?
8. **Board id.** Which id in `0x40`-`0x7F`, allocated when the port starts?
   The port's first slice took `0x41` provisionally, and so did the `stm32h5`
   and `nrf54l15` rows on their branches; the image trailer carries the id so
   that an image built for one board is refused on another, so at most one
   board could keep it. **Answered:** the Cortex-M ids are assigned in
   [`../boards/board-id-registry.md`](../boards/board-id-registry.md) in the
   order `imx95` `0x41`, `mps2-an500` `0x42`, `stm32h5` `0x43`, `rp2350` `0x44`,
   `nrf54l15` `0x45`, `mcxn947` `0x46`; this board takes `0x44`.
9. **The TCG DICE text is not in `docs/reference/`.** [DICE-HW] and [DICE-AA] are
   listed by title and URL only, because their licence forbids a copy. Is that
   enough for the port, or does the owner want another arrangement?
10. **Vendor documents.** [RP-DS] was not read here; its revision and every
    section marked "to confirm" are filled by the port's first slice.
