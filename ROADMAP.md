# Roadmap

> **Status: REFERENCE** · Last verified: 2026-10-17
> What is complete and what remains, in milestone order. Features and requirements: [docs/SPEC.md](docs/SPEC.md).

## Mission

Bare-metal firmware for the trusted platform: one wire protocol, one Go SDK. Devices prove what they are (DICE) and enroll into the platform. AVR first.

## Milestones

- [Dev complete](../../milestone/35) — RC — 2026-11-11 — 12 open, 5 closed

## Complete

- **wire-protocol** — wire constants under the new names, CAPS pin descriptor, board-id registry — [#3](../../issues/3), [#12](../../issues/12) — done
- **device-capabilities** — architecture-neutral structure, HAL free of AVR types, reset cause, lint gates — [#21](../../issues/21), [#9](../../issues/9), [#10](../../issues/10), [#13](../../issues/13) — done
- **boards** — `ARCH` from the board profile; ATmega328P, ATmega2560, ATmega32U4, ATtiny85 — [#8](../../issues/8), [v0.3.0-alpha](../../releases/tag/v0.3.0-alpha) — done
- **secure-boot** — boot geometry and timing from the board, `boot.mk` freed of AVR-only flags; P-256 verify on every row, the 2560 soft base row fit at 303 s per boot (item 79) — [#11](../../issues/11), [#25](../../issues/25) — done
- **dice-attestation** — DeviceID possession ADR, `ATTEST_CSR` (0x64) — [#6](../../issues/6), [#30](../../issues/30) — done
- **device-provisioning** — provisioning commands and `tfwprov` — [v0.3.0-alpha](../../releases/tag/v0.3.0-alpha) — done
- **host-sdk** — `tfw_` rename, `Device.AttestCSR`, host rebuild on header change, `sdk/go` — [#2](../../issues/2), [#31](../../issues/31), [#29](../../issues/29), [sdk/go/v0.3.0-alpha](../../releases/tag/sdk/go/v0.3.0-alpha) — done

## Now

- **ethernet-transport** — the wire protocol over a W5100 or W5500 on the Nano and the Mega 2560, the 328P only with a secure element (owner ruling 2026-10-17 (m522), item 79; its soft-crypto Ethernet node is withdrawn); closes AVR — [#55](../../issues/55) — open
- **xkms-adapter** — AVR backend registered through go-xkms's hook, finalize through `Device.AttestCSR` — [#28](../../issues/28) — open
- **xkms-adapter** — AVR enrollment into go-trusted-platform, through the adapter — [#20](../../issues/20) — open
- **xkms-adapter** — downstream migration to the adapter — [#19](../../issues/19) — open
- **xkms-adapter** — physical-board enrollment run end to end — [#7](../../issues/7) — open
- **application-sdk** — the application interface for `trusted-firmware-<application>` repositories — [#51](../../issues/51) — open
- **esp32-p4** — ESP32-P4 on a PoE Ethernet board, on Espressif's QEMU until the board arrives — [#48](../../issues/48) — open
- **esp32** — the ESP-IDF architecture port, the ESP32-P4 first — [#23](../../issues/23) — open
- **esp32** — ADR: ESP-IDF integration boundary and transport — [#16](../../issues/16) — done
- **ota** — signed whole-image updates with rollback, from go-trusted-platform's control plane — [#52](../../issues/52) — open
- **ota** — network and serial OTA on the AVR boards — [#75](../../issues/75) — open
- **imx95** — i.MX 95 Cortex-M7 on the FRDM-IMX95-PRO, the cortex-m reference part; landed, host and CI build only: the board as data (id `0x41`, provisional) and its TCM map (slice 1), the SCMI client (slice 2), LPUART3, RGPIO and WDOG5 on a register model (slice 3), and the enclave's message layer, session, key store, P-256 key, sign, export and verify (part of slice 5); waits: the run on the board, tier value 5 (slice 4), the enclave provider (slice 5), the OTA gate (slice 6), Ethernet (slice 7) and the owner's questions 3, 4, 19 to 21 ([imx95-design.md](docs/multi-arch/imx95-design.md), Row status) — [#49](../../issues/49) — open

## Next

None queued.

## Later

None planned.

## Future

- **secure-element-se05x** — the SE052F element tier — [docs/multi-arch/](docs/multi-arch/architecture.md) — blocked: AVR gate (#7), which waits on the soft DICE root ([#130](../../issues/130)) and go-xkms's signing key, not on the element
- **hsm-self-defence** — i.MX 95 M7 tamper-response enforcement under tenant-signed policies — [#50](../../issues/50) — blocked: AVR gate (#7)
- **stm32h5** — STM32H5, the second Cortex-M target, with the SE052F as the reference trusted device — [docs/multi-arch/](docs/multi-arch/architecture.md) — next: docs-only onboarding ADR [#126](../../issues/126); port: after [#79](../../issues/79), the P4's composed vendor-boot proof (ruling 269)
- **optiga-trust-m** — the OPTIGA Trust M element tier — [docs/multi-arch/](docs/multi-arch/architecture.md) — next: the provider (signing, the element tier's board and config) on the read-only command layer in `src/crypto/optiga/`; port blocked: AVR gate (#7), which waits on [#130](../../issues/130) and go-xkms's signing key, not on the element
- **rp2350** — RP2350 on the Pico 2 W — [docs/multi-arch/](docs/multi-arch/architecture.md) — next: docs-only onboarding ADR [#128](../../issues/128); port blocked: AVR gate (#7)
- **nrf54l15** — nRF54L15 on the nRF54L15-DK — [docs/multi-arch/](docs/multi-arch/architecture.md) — next: docs-only onboarding ADR [#129](../../issues/129); port: after the generic cortex-m HAL ([#161](../../issues/161)); not in ruling 269's hardware order
- **mcxn947** — MCXN947 on the FRDM-MCXN947 — [docs/multi-arch/](docs/multi-arch/architecture.md) — blocked: AVR gate (#7)
- **optee-trustzone** — OP-TEE / Arm TrustZone-A: the trusted-firmware side of a TrustZone-A secure world — owner ruling 2026-10-15 (go-enclave #206) — future: no milestone, no lane; go-enclave owns its arm-trustzone row
- **cortex-m** — the Cortex-M architecture ports, Cortex-M7 and Cortex-M33 first — [#22](../../issues/22) — in progress: the generic HAL and boot-HAL port on QEMU mps2-an505 ([#161](../../issues/161)), first slice landed (P-256 soft boot, `make integration-test-cortex-m` in CI), then hardware in the order SE052F, i.MX 95 M7, STM32H5 (ruling 269)
- **cortex-m** — ADR: Cortex-M reference part and CI emulation target — [#14](../../issues/14) — ungated, decided against the STM32H573I-DK and nRF54L15-DK (ruling 269)
- **cortex-m** — ADR: Cortex-M trust model and the relation to TF-M — [#15](../../issues/15) — ungated, decided against the STM32H573I-DK and nRF54L15-DK (ruling 269)
- **device-onboarding** — DICE-only devices enroll by device profile through the adapter seam — [#53](../../issues/53) — open; T1 (`tfwprov --record`) and T2 done, T3 and T4 wait on the owner

## Dependencies

- go-xkms tags the backend registry hook — done; pinned go-xkms v0.57.0-alpha in `xkms/go.mod`.
- Here: [#28](../../issues/28) phases B and C, tagged `xkms/v0.2.0-alpha`.
- go-quicbaas registers the adapter in its composition — [go-trusted-platform#771](../../../go-trusted-platform/issues/771), [go-xkms#1440](../../../go-xkms/issues/1440).
- go-xkms removes its AVR backend — [go-xkms#1439](../../../go-xkms/issues/1439).
- go-quicbaas drops its indirect require — [go-quicbaas#1185](../../../go-quicbaas/issues/1185).
- go-xkms profiles the possession CSR and verifies the DICE evidence — [go-xkms#1441](../../../go-xkms/issues/1441).
- go-trusted-platform enrolls devices — [go-trusted-platform#696](../../../go-trusted-platform/issues/696), [go-trusted-platform#772](../../../go-trusted-platform/issues/772).
- Downstream: go-xkms and go-trusted-platform import `sdk/go`, so it keeps zero in-house dependencies.
- Standards watch: draft-ietf-suit-manifest, pinned -37 — `src/core/suit.c`, `tools/suit-emit/`.
- Standards watch: draft-ietf-suit-mti, pinned -23 — `src/core/suit.c` (the ES256 profile's authentication half).
