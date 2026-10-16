# nRF54L15-DK

> **Status: INFORMATIVE** · SPEC rows: `nrf54l15`, `cortex-m` (future) · Scope:
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

- **[UG]** *nRF54L15 DK Hardware*, Nordic Semiconductor user guide (PDF dated
  2025-03-07; document version **not confirmed**). It describes the board
  PCA10156 and mentions DK v0.9.1. Sections cited by title.
- **[PS]** *nRF54L15 | nRF54L10 | nRF54L05 Datasheet*, Nordic Semiconductor
  (docs.nordicsemi.com, `ps_nrf54l15`; version **not confirmed**). Sections
  cited by title.

## The board

| | | Source |
|---|---|---|
| Board id | not allocated (Cortex-M range `0x40`-`0x7F`, [`board-id-registry.md`](board-id-registry.md)) | |
| MCU | nRF54L15 SoC, QFN48 package, on PCA10156 | [UG], introduction |
| Core | Arm Cortex-M33; clock **not confirmed** | [PS], "Arm Cortex-M33 CPU" |
| Non-volatile memory | RRAM, not flash. The family spans 500 KB to 1524 KB of NVM; the nRF54L15's own figure is **not confirmed** from the section read | [PS], "Key features"; "NVM — Non-volatile memory" |
| RAM | the family spans 96 KB to 256 KB; the nRF54L15's own figure is **not confirmed** from the section read | [PS], "Key features" |
| Debug probe | SEGGER J-Link OB programmer/debugger on the board, over USB, which also powers the board; a 2x5 SWD Debug In connector and a 20-pin trace footprint. The interface MCU's part number is **not confirmed** | [UG], introduction; "Debugger"; "Debug input and trace" |
| Console to the host | two UARTs through the J-Link OB's virtual serial ports. The debugger's UART pins are tri-stated until a terminal opens the port and asserts DTR; RTS/CTS are free for other use when hardware flow control is off; the signals pass through analog switches set from the Board Configurator | [UG], introduction; "Virtual serial ports" |
| Console pins | the board labels them UART0 (TXD, RXD, RTS, CTS) and UART1; the nRF54L15 GPIO for each is **not confirmed** (the table did not extract legibly) | [UG], "Hardware description" connector tables |
| Ethernet | **none**. Radios: Bluetooth Low Energy, 802.15.4 (Thread), 2.4 GHz proprietary, NFC | [UG], introduction |
| Power | nPM1300 PMIC, 1.8 V to 3.3 V user-programmable from USB | [UG], introduction |

## Security facilities

As Nordic names them, from [PS] section titles and their summaries. Register
detail is **not confirmed**.

| | |
|---|---|
| TrustZone-M | hardware isolation between secure and non-secure resources as defined by Arm TrustZone, with peripherals assignable to either world ("TrustZone security"; "Peripherals with split security access") |
| KMU | Key management unit: keys in a dedicated RRAM region; the KMU and its key slots in SICR are accessible from secure mode only ("KMU — Key management unit") |
| CRACEN | Cryptographic accelerator engine. With the KMU, keys can be pushed to CRACEN so they stay protected even from secure-world code; it has DPA countermeasures ("CRACEN — Cryptographic accelerator engine"). Which curves it accelerates is **not confirmed** |
| Boot | an immutable boot region ("Immutable boot region"); Nordic does not call it "secure boot" in the section read |
| Tamper | TAMPC tamper controller and voltage glitch detectors ("TAMPC — Tamper controller"; "GLITCHDET — Voltage glitch detectors") |

## Upstream TF-M

As ADR #15 states it ("Platforms upstream at v2.3.1"): there is **no nRF54L15
port in upstream TF-M** at v2.3.1. Nordic carries TF-M for the part in its own
SDK.

## What this page bears on

- **ADR #15 open question 2** (vendor secure worlds): with no upstream port,
  this part's secure world is Nordic's TF-M or none, so the licence check of
  Nordic's SDK is on this board's path.
- **ADR #15 open question 4** (crypto tier values): a KMU key used through
  CRACEN is hidden even from secure code, which is a stronger claim than
  "TrustZone-isolated key".
- **The `nrf54l15` row's enrolment**: there is no Ethernet on this board.

## What is not verified

The datasheet's per-part memory figures, CRACEN's algorithms and the console
pin map. The first slice of a port reads them and replaces every
"not confirmed" here.
