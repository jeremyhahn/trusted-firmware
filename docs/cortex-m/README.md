# Cortex-M

> **Status: DRAFT** · Issue: #161 · SPEC row: `cortex-m` (future) · Last verified: 2026-10-20

The Cortex-M architecture ports: Cortex-M7 and Cortex-M33 first. Each is a HAL
and boot-HAL port under `src/hal/cortex-m/` and `boot/hal/cortex-m/`, and boards
are data. The owner ungated the two ADRs on 2026-10-18 (CA m623); the first
slice is the generic HAL and boot-HAL port on QEMU `mps2-an505` (#161, below),
then hardware in the SPEC row's order.

| Document | What it decides |
|---|---|
| [`adr-reference-part-and-emulation.md`](adr-reference-part-and-emulation.md) | **PROPOSED** (#14): the i.MX 95 Cortex-M7 as the reference part, checked against the STM32H573I-DK and the nRF54L15-DK; QEMU `mps2-an505` as the CI target; the HAL mapping, toolchain, budgets and boot chain |
| [`adr-trust-model-vs-tf-m.md`](adr-trust-model-vs-tf-m.md) | **PROPOSED** (#15): the trust model and its relation to TF-M — TF-M Profile Medium with our attestation partition on the M33, MPU-only isolation on the M7, where the DICE root lives on each part, and the owner's open questions |

## What is built (#161)

The generic port and its first board, `boards/mps2-an505`: Arm's AN505
(a Cortex-M33) as QEMU models it, soft tier only, board id `0x40`
(allocated in [`board-id-registry.md`](../boards/board-id-registry.md)). The architecture's make file is `mk/arch/cortex-m.mk` and the
SoC's linker script is `mk/arch/cortex-m/mps2-tz.ld`. The core HAL is in
`src/hal/cortex-m/`, with the MPS2 peripherals in `mps2-tz/`, and tfw-boot's
hardware layer is in `boot/hal/cortex-m/`. The board builds the application
(`make firmware BOARD=mps2-an505 CRYPTO=soft`) and one tfw-boot row: soft
tier and measurement, P-256 verified in software, no OTA, no store.

**The emulation test.** `make integration-test-cortex-m`
(`mk/arch/cortex-m-targets.mk`) boots both images in `qemu-system-arm -M
mps2-an505`. The signer `test/integration/cortex-m/cm_image.c` provisions a
fresh key and UDS into tfw-boot and signs the application.
`test/integration/cortex-m/qemu_test.sh` runs three QEMUs:

- the signed image answers CAPS (board `0x40`, tier 1), takes a soft reset
  and answers again;
- a tampered image stays silent for a bounded window with QEMU still up;
- an image signed with another key does the same.

CI runs it in the image job, in the toolchain image, as the step
`make integration-test-cortex-m (toolchain image)`. Two steps before it build
the two images. `firmware-matrix` builds `mps2-an505` with the rest of the
boards. The Arm toolchain and QEMU are in `docker/packages.txt`.

QEMU has no i.MX 95, so this proves the core, tfw-boot and the wire on a
32-bit core, never a vendor SoC. The i.MX 95's Cortex-M7 is the board
`frdm-imx95-m7` (SPEC row `imx95`). `firmware-matrix` builds it, but nothing
runs it. Its map and SoC directory are in
[`../multi-arch/imx95-design.md`](../multi-arch/imx95-design.md#9-the-board-as-built-frdm-imx95-m7).

## Bench boards

The Cortex-M boards on the bench have profile pages, with vendor citations and
no port: [`stm32h573i-dk.md`](../boards/stm32h573i-dk.md),
[`rp2350-pico2w.md`](../boards/rp2350-pico2w.md) and
[`nrf54l15-dk.md`](../boards/nrf54l15-dk.md). None has a board id. The vendor
documents the ADRs cite are listed in
[`../reference/README.md`](../reference/README.md), "Cortex-M sources".

The multi-architecture plan
these ports belong to is [`../multi-arch/architecture.md`](../multi-arch/architecture.md).
