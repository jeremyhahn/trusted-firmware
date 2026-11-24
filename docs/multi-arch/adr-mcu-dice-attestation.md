# ADR: one MCU attestation path, TCG DICE through device-attest-01 in the secure world

> **Status: ACCEPTED** · Issue:
> #586 · SPEC rows: `stm32h5`, `cortex-m`, `esp32-p4` · Scope:
> trusted-firmware (the MCU secure worlds and their evidence); go-xkms (what it
> verifies, seam only) · Related: ADR #15
> ([`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md)),
> the STM32H573I-DK onboarding ADR
> ([`adr-stm32h573i-dk-onboarding.md`](adr-stm32h573i-dk-onboarding.md)),
> board pages [`../boards/stm32h573i-dk.md`](../boards/stm32h573i-dk.md),
> [`../boards/nucleo-h563zi.md`](../boards/nucleo-h563zi.md),
> [`../boards/nrf54l15-dk.md`](../boards/nrf54l15-dk.md),
> [`../boards/esp32p4-poe.md`](../boards/esp32p4-poe.md)
>
> This ADR records a decision. Nothing it describes for the STM32H5, the
> nRF54L15 or the ESP32-P4 is built yet; the slices that build it are listed
> at the end as future rows. It changes no code, no wire vector and no SPEC
> row: aligning the `stm32h5` row in `docs/SPEC.md` is a later slice that
> needs a policy grant.

## Amended by ruling 1024 (owner, 2026-11-20)

The owner decided on 2026-11-20,
verbatim:

> "the TrustZone-M secure world is built on Trusted Firmware-M with our DICE
> layer on top, not a secure monitor written from scratch."

On the STM32H5 and the nRF54L15 the secure world is therefore ADR #15's (b1):
upstream TF-M Profile Medium, with our DICE layer (the CDI, the DeviceID key
and the alias key) as an Application RoT partition. That replaces "our own
TrustZone-M firmware" in ruling 935 below. The rest of ruling 935 stands: one
DICE attestation path through go-xkms device-attest-01, no vendor secure world
(not ST's Secure Manager), no PSA token and no PSA or EAT verifier. TF-M is
built without its Initial Attestation partition, or with it present but not
exposed to the non-secure side. The per-MCU table and the consequences below
are amended to match; ADR #15's
[amendment](../cortex-m/adr-trust-model-vs-tf-m.md#amended-by-ruling-1024-owner-2026-11-20)
says the same.

## Context

The `stm32h5` row in [`../SPEC.md`](../SPEC.md) leaves one design decision
open, verbatim:

> "One design decision is open: ST's Secure Manager as the secure world, with
> its PSA attestation token verified by go-xkms's EAT path, or our own
> TrustZone firmware."

ADR #15 carried the same choice as its open questions 2 (vendor secure
worlds) and 4 (the PSA token), and the STM32H573I-DK onboarding ADR as its open
questions 1, 2 and 5. Each MCU family otherwise risked its own attestation
path: a PSA token on the H5, Nordic's TF-M on the nRF54L, the HMAC peripheral
on the ESP32, the element tier on AVR.

## Decision

Ruling 935 (owner queue 236), verbatim:

> "One attestation path for every MCU: TCG DICE evidence through go-xkms
> device-attest-01 (the dice format of draft-ietf-acme-device-attest-10), with
> the DICE layer and the DeviceID and alias keys inside the secure world: our
> own TrustZone-M firmware on STM32H5 and nRF54L (not ST Secure Manager, no PSA
> token verifier); ESP-TEE on ESP32-P4 once ESP-IDF supports the P4, otherwise
> the current HMAC-peripheral design; no firmware TPM on microcontrollers."

### Per MCU

| MCU | Secure world | DICE layer and keys |
|---|---|---|
| STM32H5: NUCLEO-H563ZI and STM32H573I-DK | upstream TF-M Profile Medium; not ST's Secure Manager | our DICE layer, an Application RoT partition in TF-M, holds: the CDI derived from the UDS and the measurement of the next layer, the DeviceID key and the alias key. None of them is exported to the non-secure side, which reaches them only through the partition's PSA client calls over TF-M's non-secure-callable (NSC) veneers |
| nRF54L15 | upstream TF-M Profile Medium, as on the H5 | the same DICE partition, with the keys held through the part's Key Management Unit (KMU) |
| ESP32-P4 | ESP-TEE, once ESP-IDF supports the P4 | until then the current HMAC-peripheral design ([`esp32-p4-design.md`](esp32-p4-design.md)): the CDI from the eFuse UDS through the HMAC peripheral |
| every MCU | no firmware TPM | |

### Verifier side

The evidence is the dice format of draft-ietf-acme-device-attest-10, verified
by go-xkms's device-attest-01. The device signs the same three fixed structures
and carries `tcg-dice-UCCS-evidence` in the alias certificate, as
[`../dice-attestation/architecture.md`](../dice-attestation/architecture.md)
describes. No PSA token verifier and no EAT verifier is added for this path.

## Consequences

Dropped:
- ST's Secure Manager as the secure world on the STM32H573I-DK, and with it the
  export-controlled access kit and its licence check (onboarding ADR open
  question 5).
- ST's factory device certificate as the identity root; the DeviceID is ours,
  derived in our secure world. Whether the part carries such a certificate
  stays a hardware fact on the board page, not a dependency.
- The PSA attestation token and its verifier on go-xkms's EAT path (ADR #15
  open question 4), and the IAK-to-DeviceID binding that would have needed.
- *Superseded by ruling 1024:* on the STM32H5 and the nRF54L15, ADR #15's (b1)
  (TF-M Profile Medium with our partition) was dropped, because ruling 935
  named our own TrustZone-M firmware there, ADR #15's option (a). Ruling 1024
  takes (b1) back and drops (a): the secure world is upstream TF-M, built
  without its Initial Attestation partition or with it not exposed.

Stays:
- The existing DICE work: the TCB info, the three template builders, the UCCS
  evidence and the device-attest-01 alias
  ([`../dice-attestation/architecture.md`](../dice-attestation/architecture.md)).
- The AVR path, unchanged.
- The ESP32-P4's current HMAC-peripheral design, until ESP-TEE supports the P4.
- The hardware facts on each board page.

## Implementation slices

Future rows; none is built.

Under ruling 1024, slices (a) and (b) are built on TF-M: (a) is TF-M's own
secure/non-secure split for the board, not one written here (what row #590's own
split is kept for is settled in row #608's slices), and (b) is the DICE layer as a
TF-M Application RoT partition. Row #608 carries the TF-M slices; its plan is
[`../plans/608.md`](../plans/608.md).

- (a) The STM32H5 TrustZone-M secure/non-secure split and NSC veneers on the
  NUCLEO-H563ZI, in simulation and host tests.
- (b) The secure-world DICE layer (CDI, DeviceID key, alias key) behind the NSC
  API, host-tested.
- (c) device-attest-01 dice evidence from the H5, checked against go-xkms in a
  host test.
- (d) The same on the STM32H573I-DK on hardware; probe-dependent (J-Link or
  Raspberry Pi Debug Probe).
- (e) The nRF54L15 with the KMU; probe-dependent.
- (f) ESP32-P4 HMAC-peripheral evidence in the dice format, with ESP-TEE gated
  on ESP-IDF support for the P4.
