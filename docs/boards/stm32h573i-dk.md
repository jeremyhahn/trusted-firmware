# STM32H573I-DK

> **Status: INFORMATIVE** · SPEC rows: `stm32h5`, `cortex-m` (future) · Scope:
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

**ST's documents could not be retrieved on 2026-10-16**: every download from
st.com timed out or was refused. The values below come from the parts of ST's
documents that are indexed publicly (the introduction of UM3143, the product
pages), and each says so. Every revision and section number for an ST document
is therefore **not confirmed**, and a port starts by reading the PDFs.

## The board

| | | Source |
|---|---|---|
| Board id | not allocated (Cortex-M range `0x40`-`0x7F`, [`board-id-registry.md`](board-id-registry.md)) | |
| MCU | STM32H573II | UM3143, *Discovery kit with STM32H573II MCU*, introduction; revision and section not confirmed |
| Core | Arm Cortex-M33 with TrustZone, 250 MHz | ST product page *STM32H573II* (a web page, no document number); not confirmed against the datasheet DS14053 |
| Flash | 2 Mbytes on chip | ST product page *STM32H573II*; not confirmed against DS14053 |
| RAM | 640 Kbytes | ST product page *STM32H573II*; not confirmed against DS14053 |
| Debug probe | STLINK-V3EC on the board, with mass storage, a Virtual COM port and a debug port | UM3143, introduction (features); also the data brief *STM32H573I-DK* (DB number and revision not confirmed) |
| Console to the host | the STLINK-V3EC Virtual COM port over its USB connector | UM3143, introduction |
| Console USART and pins | **not confirmed**. UM3143 names them in its Virtual COM port section, which could not be read | |
| Ethernet | **on the board**: 10/100 Mbit/s, IEEE 802.3-2002, from the MCU's Ethernet MAC | UM3143, introduction (features) |
| Ethernet PHY part and connector | **not confirmed** | |
| Other radios | a Wi-Fi module | UM3143, introduction |

## Security facilities

| | | Source |
|---|---|---|
| TrustZone-M | yes: Armv8-M with the Security Extension | ST product page *STM32H573II*; not confirmed against DS14053 |
| ST's secure world | **STM32Trust TEE Secure Manager** (STM32TRUSTEE-SM): a PSA-compliant secure world for Armv8-M, with secure boot, cryptography, internal trusted storage, initial attestation and firmware update | UM3254, *Secure Manager for STM32H573xx microcontrollers*, Rev 8, August 2025; section not confirmed. Feature list also on ST's STM32TRUSTEE-SM page |
| Secure Manager access kit | X-CUBE-SEC-M-H5, distributed under export-control conditions | ST's STM32TRUSTEE-SM page |
| Certification targets | PSA Certified Level 3 and SESIP 3 are the part's targets; the SESIP guidance is UM3238 | ST product page *STM32H573II*; UM3238, *STM32H573xx STM32TrusteeSM security guidance for SESIP 3 certification*, revision not confirmed |
| Factory device certificate | **not confirmed** (the `stm32h5` row states one; no ST document was read that says so) | |
| Hardware crypto (PKA, AES, HASH) | **not confirmed** | |

## Upstream TF-M

As ADR #15 states it ("Platforms upstream at v2.3.1"): the STM32H573I-DK is an
upstream TF-M platform at **v2.3.1**; the NUCLEO-H563ZI is not.

## What this page bears on

- **ADR #15 open question 2** (vendor secure worlds): the Secure Manager is
  documented in UM3254, and its access kit is export-controlled. The licence
  check that question asks for starts there.
- **ADR #15 open question 1**: the upstream TF-M platform is this board, not
  the NUCLEO-H563ZI.
- **The `stm32h5` row's "enrolling over Ethernet"**: this board has Ethernet on
  board, so no W5x00 module is needed for it.

## What is not verified

Everything ST-sourced above is from indexed summaries of ST's documents, not
from the documents. The first slice of a port reads DS14053, RM0481, UM3143 and
UM3254 at their current revisions and replaces every "not confirmed" here.
