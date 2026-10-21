# Cortex-M

> **Status: PLANNED** · SPEC row: `cortex-m` (future) · Last verified: 2026-10-20

The Cortex-M architecture ports: Cortex-M7 and Cortex-M33 first. Each is a HAL
and boot-HAL port under `src/hal/cortex-m/` and `boot/hal/cortex-m/`, and boards
are data. Nothing here is implemented yet. The owner ungated the two ADRs on
2026-10-18 (CA m623); the next slice is the generic HAL and boot-HAL port on QEMU
`mps2-an505` (#161), then hardware in the SPEC row's order.

| Document | What it decides |
|---|---|
| [`adr-reference-part-and-emulation.md`](adr-reference-part-and-emulation.md) | **PROPOSED** (#14): the i.MX 95 Cortex-M7 as the reference part, checked against the STM32H573I-DK and the nRF54L15-DK; QEMU `mps2-an505` as the CI target; the HAL mapping, toolchain, budgets and boot chain |
| [`adr-trust-model-vs-tf-m.md`](adr-trust-model-vs-tf-m.md) | **PROPOSED** (#15): the trust model and its relation to TF-M — TF-M Profile Medium with our attestation partition on the M33, MPU-only isolation on the M7, where the DICE root lives on each part, and the owner's open questions |

The Cortex-M boards on the bench have profile pages, with vendor citations and
no port: [`stm32h573i-dk.md`](../boards/stm32h573i-dk.md),
[`rp2350-pico2w.md`](../boards/rp2350-pico2w.md) and
[`nrf54l15-dk.md`](../boards/nrf54l15-dk.md). None has a board id. The vendor
documents the ADRs cite are listed in
[`../reference/README.md`](../reference/README.md), "Cortex-M sources".

The multi-architecture plan
these ports belong to is [`../multi-arch/architecture.md`](../multi-arch/architecture.md).
