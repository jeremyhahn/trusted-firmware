# Raspberry Pi Pico 2 W (RP2350)

> **Status: INFORMATIVE** · SPEC rows: `rp2350`, `cortex-m` (future) · Scope:
> none yet, no port exists · Last verified: 2026-10-16
>
> A profile of a board on the bench, not of a build. Nothing here has been
> measured, because nothing has been built: the `cortex-m` row waits on #7. Each
> value is cited to the vendor's document. A value that could not be read from
> one is written **not confirmed**. The normative contract a port will follow is
> [`adding-a-board.md`](adding-a-board.md); the decisions it will follow are
> ADR #14 ([`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md))
> and ADR #15 ([`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md)),
> both PROPOSED.

Documents cited:

- **[PB]** *RP2350* product brief, Raspberry Pi Ltd, RP-008374-DS-1, published
  August 2024.
- **[DOC]** Raspberry Pi documentation, *Pico-series microcontrollers*, section
  "Raspberry Pi Pico 2 W" (web page, retrieved 2026-10-16). It links the board
  datasheet below.
- **[DS]** *Raspberry Pi Pico 2 W Datasheet: An RP2350-based microcontroller
  board*, RP-008304-DS (file RP-008304-DS-2). Its body text could not be
  extracted; only its section headings were read. Its release history lists
  25 November 2024 and 03 July 2026.

## The board

| | | Source |
|---|---|---|
| Board id | not allocated (Cortex-M range `0x40`-`0x7F`, [`board-id-registry.md`](board-id-registry.md)) | |
| MCU | RP2350; the board's package variant is **not confirmed** | [DOC] |
| Core | dual Arm Cortex-M33 with FPU, or dual Hazard3 RISC-V, selectable, at 150 MHz | [PB], "Key features"; [DOC] |
| Flash | **none on chip** for the RP2350A and RP2350B (the RP2354 variants carry 2 MB stacked); the board carries **4 MB** of external QSPI flash | [PB], package table; [DOC] |
| RAM | 520 KB on-chip SRAM, in ten banks | [PB], "Key features"; [DOC] |
| Debug probe | **none on the board**: a three-pin SWD connector (SWCLK, GND, SWDIO, no power pin), as through-hole pads or, on the "with headers" variant, a keyed three-pin JST-style connector. An external probe is needed | [DOC]; [DS] §3.9 "Debugging" (heading only read) |
| Console to the host | **not confirmed**. The board has a Micro-USB port to the RP2350's USB 1.1 controller ([DOC]), and the RP2350 has two UARTs ([PB]); no USB-to-UART bridge is described. Which UART and pins a port uses is a port decision | [PB]; [DOC] |
| Ethernet | **none**. Wireless only: Infineon CYW43439, 2.4 GHz 802.11n Wi-Fi and Bluetooth 5.2, on SPI at up to 33 MHz | [DOC]; [DS] §3.8 "Wireless interface" (heading only read) |

## Security facilities

All from [PB], "Overview" and "Key features". The RP2350 datasheet
(RP-008373-DS), whose security chapter is the register-level source, was not
read; its section numbers are **not confirmed**.

| | |
|---|---|
| TrustZone-M | the security architecture is "built around Arm TrustZone for Cortex-M" |
| OTP | 8 KB of antifuse OTP for key storage |
| Secure boot | optional boot signing, enforced by the on-chip mask ROM, with the key fingerprint in OTP |
| Boot decryption | protected OTP storage for an optional boot decryption key |
| Isolation | global bus filtering on Arm or RISC-V security and privilege levels; peripherals, GPIOs and DMA channels individually assignable to security domains |
| Crypto | a hardware SHA-256 accelerator and a hardware TRNG. **No public-key accelerator is named**, so P-256 signing would be in software |
| Fault injection | fast glitch detectors and hardware mitigations for fault-injection attacks |
| Documentation | the vendor states the security features, including the secure boot ROM, are documented without restriction |

## Upstream TF-M

As ADR #15 states it ("Platforms upstream at v2.3.1"): the RP2350 is an
upstream TF-M platform at **v2.3.1**.

## What this page bears on

- **ADR #15 open question 4** (crypto tier values): the key store here is OTP
  plus TrustZone, with no hardware ECC, which is neither AVR's element tier nor
  an SoC enclave.
- **ADR #15 open question 5** (the alias key): with software P-256 the M33 can
  derive a per-boot alias key from a CDI, which the ADR's context assumes.
- **The `rp2350` row's enrolment**: there is no Ethernet on this board, so a
  port enrols over Wi-Fi, USB or a UART, or adds a W5x00 module; which is not
  decided here.

## What is not verified

The board datasheet's body text and the RP2350 datasheet were not read. The
first slice of a port reads both and replaces every "not confirmed" here.
