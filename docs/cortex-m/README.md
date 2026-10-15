# Cortex-M

> **Status: PLANNED** · SPEC row: `cortex-m` (future) · Last verified: 2026-10-15

The Cortex-M architecture ports: Cortex-M7 and Cortex-M33 first. Each is a HAL
and boot-HAL port under `src/hal/cortex-m/` and `boot/hal/cortex-m/`, and boards
are data. Nothing here is implemented yet. Work starts only once AVR has every
primitive and a proven physical end-to-end run (#7).

| Document | What it decides |
|---|---|
| [`adr-reference-part-and-emulation.md`](adr-reference-part-and-emulation.md) | **PROPOSED** (#14): the i.MX 95 Cortex-M7 as the reference part, QEMU `mps2-an500` as the CI target, the HAL mapping, toolchain, budgets and boot chain |
| [`adr-trust-model-vs-tf-m.md`](adr-trust-model-vs-tf-m.md) | **PROPOSED** (#15): the trust model and its relation to TF-M — TF-M Profile Medium with our attestation partition on the M33, MPU-only isolation on the M7, and the owner's open questions |

The multi-architecture plan
these ports belong to is [`../multi-arch/architecture.md`](../multi-arch/architecture.md).
