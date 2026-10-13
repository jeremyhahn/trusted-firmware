# trusted-firmware — specification

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-10-13
> Conflicts with this document are defects. Fix them here first.

## Purpose

Bare-metal ANSI C firmware for classic AVR, and `sdk/go`, the Layer 0 Go SDK that drives it.
A drop-in replacement for basic Arduino firmware: every Arduino capability is on the wire and in the SDK, with secure boot, a DICE device identity and go-trusted-platform integration added.
It carries no application logic. go-xkms and go-trusted-platform import `sdk/go`; the `xkms` adapter module plugs the AVR device into go-xkms's backend registry.

## Boundaries

- Owns the AVR wire protocol and the firmware that speaks it.
- Owns `sdk/go` (host-sdk): Layer 0, zero in-house dependencies, no private keys; consumed by go-trusted-platform at `v0.3.0-alpha`.
- Owns the `xkms` adapter module (xkms-adapter, `github.com/jeremyhahn/trusted-firmware/xkms`) and all AVR-specific xkms code; it provides the AVR backend to go-xkms backend-registry, a seam confirmed on both sides, and go-trusted-platform and xkey blank-import it.
- go-xkms holds generic DICE, EAT, device-attest-01, the verifiers, the backend interfaces and the registration hook, and nothing AVR-specific.
- go-quicbaas has a generic verifier and no firmware dependency.
- Never owns DICE ASN.1, certificate profiles, DICE verification or attestation verification: go-xkms owns them.
- Never owns device enrollment, the device registry or fleet policy: go-trusted-platform owns them.
- A gap in go-xkms or go-trusted-platform is filed against that repo, never worked around here.
- Never decodes a sensor: the device offers capability, the host supplies meaning.
- Never described as "TCG DICE compliant": several DICE hardware requirements are met only procedurally on a classic AVR.

## Features

| Feature | Status | Requirements | Detail |
|---|---|---|---|
| wire-protocol | shipped | Frames every host–device exchange over UART, CRC-16 checked, with resync. Command ids and status codes extend and never renumber. Literal golden vectors hold the C and Go codecs to the same bytes. | docs/wire-protocol/ |
| device-capabilities | shipped | Exposes GPIO, the pin scheduler, analog read, PWM, tone, pulse, EEPROM, I²C, SPI and 1-Wire as wire commands. The HAL is substituted at link time, never through a vtable. A pin told to hold a level reverts on time whether or not the host is present. There is no `delay()`. | docs/device-capabilities/ |
| boards | shipped | A board is data: a profile, pin tables and build budgets; adding one touches nothing under `src/` unless the part lacks a peripheral. ATmega328P, ATmega2560, ATmega32U4 and ATtiny85 are registered. Each board names the crypto tiers it can carry; a part with no boot section reports the none tier rather than a software tier it cannot honour. Board ids come from the board-id registry. | docs/boards/ |
| dice-attestation | shipped | The device builds its evidence: the identity from a hardware root of trust in one of the crypto tiers, the alias certificate and UCCS evidence carrying the verifier's nonce, and the device-templated PKCS#10 that proves DeviceID key possession at ACME finalize, with no signing oracle. Verifying that evidence belongs to go-xkms. | docs/dice-attestation/ |
| secure-boot | shipped | `tfw-boot` measures the application, verifies its signature, enforces the anti-rollback floor and derives the layer-0 CDI on every reset. A refusal at any step keeps the device in recovery. The build is reproducible, so the expected firmware hash is computable from source. | docs/secure-boot/ |
| device-provisioning | shipped | Takes a blank part to an identity-bearing device once, on a bench, never over the network. Provisioning commands are absent from every production image. Fuses and lock bits, the secure element and the serial are set by independent tools, none able to undo another's step. | docs/device-provisioning/ |
| host-sdk | shipped | `sdk/go` is the Layer 0 host implementation: codec, framing, transport, provisioning and attestation calls, with zero in-house dependencies. `tfwctl` and `tfwprov` are commands in that module. `tfwsim` is the real firmware built for the host, and the SDK is tested against it. | docs/host-sdk/ |
| xkms-adapter | in progress | The AVR backend for go-xkms, registered through go-xkms's backend registry hook: a read-only key provider that answers from the device's `CAPS` reply and refuses what the part cannot do. It holds no private key and reaches the wire only through `sdk/go`. Remaining (#28 phases B and C): bump go-xkms, implement `xkms/register.go`, finalize through `Device.AttestCSR`, tag `xkms/v0.2.0-alpha`. | docs/xkms-adapter/ |
| cortex-m | future | The first Cortex-M port, with ADRs for the reference part and CI emulation target and for the trust model and its relation to TF-M. Per-architecture code lives only behind the HAL, the boot HAL and the board profiles. Starts only once AVR has every primitive and a proven physical end-to-end run (#7). | docs/multi-arch/ |
| stm32h5 | future | The Cortex-M target of phase #22: a Cortex-M33 with TrustZone, enrolling over Ethernet, on two targets. The STM32H573I-DK is the reference board: hardware crypto, ST Secure Manager and a factory device certificate. The NUCLEO-H563ZI is the secondary target: it proves our own TrustZone secure world and software crypto on a part without ST's secure services, the common case across the H5, U5 and L5 lines. One design decision is open: ST's Secure Manager as the secure world, with its PSA attestation token verified by go-xkms's EAT path, or our own TrustZone firmware. | docs/multi-arch/ |
| secure-element-se05x | future | The element tier on the NXP EdgeLock SE052F, next to the ATECC608 tier. The on-device C driver is ours; the host-side Go backend is go-enclave's (its row se052f-secure-element, go-enclave #191). go-xkms sees only evidence and the NXP-issued device certificate. The tier fits the ATmega2560, ATmega32U4 and Cortex-M targets, not the ATmega328P. | docs/multi-arch/ |
| optiga-trust-m | future | An element tier on the Infineon OPTIGA Trust M, the plain OPTIGA Trust M V3 shield. The on-device driver is ours; the host-side backend is go-enclave's, the same split as the SE052F. | docs/multi-arch/ |
| rp2350 | future | The RP2350 on the Raspberry Pi Pico 2 W: the firmware core as a HAL port, with DICE evidence on the part's OTP and secure boot. Enrollment goes through go-xkms into go-trusted-platform. | docs/multi-arch/ |
| esp32-c6 | future | The ESP32-C6FH4 on the DFRobot FireBeetle 2, the phase #23 target: the firmware core as a HAL port, with DICE evidence on the part's eFuse keys and secure boot. Enrollment goes through go-xkms into go-trusted-platform. | docs/multi-arch/ |
| nrf54l15 | future | The nRF54L15 on the nRF54L15-DK: the firmware core as a HAL port, with DICE evidence on the part's KMU and TrustZone. Enrollment goes through go-xkms into go-trusted-platform. | docs/multi-arch/ |
| mcxn947 | future | The MCXN947 on the NXP FRDM-MCXN947, a Cortex-M33 with the EdgeLock secure subsystem: the firmware core as a HAL port, with DICE evidence on the part's EdgeLock secure subsystem. Enrollment goes through go-xkms into go-trusted-platform. | docs/multi-arch/ |
| esp32 | future | The ESP32 port, with an ADR for its integration boundary and transport. Per-architecture code lives only behind the HAL, the boot HAL and the board profiles. Starts only once AVR has every primitive and a proven physical end-to-end run (#7). | docs/multi-arch/ |

## Interfaces

- Wire protocol: the framed binary protocol over UART — [wire-protocol/api.md](wire-protocol/api.md).
- Boot commands and record layouts of `tfw-boot` — [secure-boot/api.md](secure-boot/api.md).
- Go SDK: `sdk/go` (host-sdk), consumed by go-trusted-platform at `v0.3.0-alpha` — [host-sdk/api.md](host-sdk/api.md).
- CLI: `tfwctl` and `tfwsim` — [host-sdk/cli.md](host-sdk/cli.md); `tfwprov` — [device-provisioning/cli.md](device-provisioning/cli.md).
- xkms adapter (xkms-adapter): provides the AVR backend to go-xkms backend-registry; go-trusted-platform and xkey blank-import it — [xkms-adapter/api.md](xkms-adapter/api.md).
- Build, test and flash: the Makefile targets — [development/building.md](development/building.md).

## Release criteria

- Release candidate: `docs/SPEC.md` and `ROADMAP.md` exist in the handbook's shapes and agree (#39).
- Release candidate: the owner approves the whole of both files.
