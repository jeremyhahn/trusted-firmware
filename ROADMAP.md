# Roadmap

> **Status: REFERENCE** · Owner: Software Department · Last verified: 2026-10-08

## Mission

Bare-metal firmware for the trusted platform: one wire protocol, one Go SDK.
Devices prove what they are (DICE) and enroll into the platform.
AVR first; other architectures only after AVR is complete and proven end to end.

## Current milestone

None yet. The Software Department sets the milestone.

## Now

- [#18](http://git.example.internal/jeremyhahn/trusted-firmware/issues/18) — Phase 1: rebrand and `v0.1.0-alpha` — in review
- [#2](http://git.example.internal/jeremyhahn/trusted-firmware/issues/2) — mechanical rename to `tfw_` / `tfwsim` / `tfwctl` / `tfwprov` — open
- [#3](http://git.example.internal/jeremyhahn/trusted-firmware/issues/3) — wire constants and vectors under the new names — open
- [#4](http://git.example.internal/jeremyhahn/trusted-firmware/issues/4) — roadmap and multi-arch architecture documents — in review
- [#5](http://git.example.internal/jeremyhahn/trusted-firmware/issues/5) — `v0.1.0-alpha` release — blocked: needs #2 and #3

## Next

- [#19](http://git.example.internal/jeremyhahn/trusted-firmware/issues/19) — Phase 2: downstream repos migrated to the new names — open
- [#20](http://git.example.internal/jeremyhahn/trusted-firmware/issues/20) — Phase 3: AVR enrollment into go-trusted-platform — open
- [#6](http://git.example.internal/jeremyhahn/trusted-firmware/issues/6) — key possession proven at ACME finalize — blocked: owner decision
- [#7](http://git.example.internal/jeremyhahn/trusted-firmware/issues/7) — end-to-end run on a physical board — open
- [go-quicbaas#1147](http://git.example.internal/jeremyhahn/go-quicbaas/issues/1147) — routing of device attestation — open
- [go-trusted-platform#696](http://git.example.internal/jeremyhahn/go-trusted-platform/issues/696) — device enrollment — open
- [#1](http://git.example.internal/jeremyhahn/trusted-firmware/issues/1) — public-release readiness — open

## Later

Gated: no other-platform work (Phases 5 and 6) until AVR has every primitive and a
proven end-to-end example (owner ruling 2026-10-08).

- [#21](http://git.example.internal/jeremyhahn/trusted-firmware/issues/21) — Phase 4: architecture-neutral structure on AVR — open
- [#8](http://git.example.internal/jeremyhahn/trusted-firmware/issues/8) — build selects the architecture through `ARCH` — open
- [#9](http://git.example.internal/jeremyhahn/trusted-firmware/issues/9) — architecture-neutral HAL contract — open
- [#10](http://git.example.internal/jeremyhahn/trusted-firmware/issues/10) — reset cause decoded behind the HAL — open
- [#11](http://git.example.internal/jeremyhahn/trusted-firmware/issues/11) — boot geometry, NVM and timing behind the boot HAL — open
- [#12](http://git.example.internal/jeremyhahn/trusted-firmware/issues/12) — wire generalisation and board-id registry — open
- [#13](http://git.example.internal/jeremyhahn/trusted-firmware/issues/13) — lint and architecture-leak gates — open
- [#22](http://git.example.internal/jeremyhahn/trusted-firmware/issues/22) — Phase 5: first Cortex-M port — blocked: Phase 4 and AVR gate
- [#14](http://git.example.internal/jeremyhahn/trusted-firmware/issues/14) — ADR: Cortex-M part and CI emulation — blocked: AVR gate
- [#15](http://git.example.internal/jeremyhahn/trusted-firmware/issues/15) — ADR: trust model and Trusted Firmware-M — blocked: AVR gate
- [#23](http://git.example.internal/jeremyhahn/trusted-firmware/issues/23) — Phase 6: ESP32 — blocked: Phase 5 and AVR gate
- [#16](http://git.example.internal/jeremyhahn/trusted-firmware/issues/16) — ADR: ESP32 root of trust — blocked: AVR gate
- [#17](http://git.example.internal/jeremyhahn/trusted-firmware/issues/17) — later targets — blocked: AVR gate

Design for the later phases: [docs/multi-arch/architecture.md](docs/multi-arch/architecture.md).

## Dependencies

- go-xkms verifies the DICE evidence (`device-attest-01`).
- go-quicbaas routes it.
- go-trusted-platform enrolls devices.
- `sdk/go` has zero in-house dependencies (Layer 0), so all three can import it.
- Ownership rules: handbook `software/docs/00-ownership-and-sdk-boundary.md`.

## Non-goals

- DICE ASN.1, certificate profiles and attestation verification: go-xkms owns them.
- Enrollment, the device registry and fleet policy: go-trusted-platform owns them.
