# FRDM-IMX95-PRO, the i.MX 95's Cortex-M7

> **Status: INFORMATIVE** · SPEC rows: `imx95`, `cortex-m`, `ota`,
> `hsm-self-defence` · Scope: `boards/frdm-imx95-m7/` and
> `src/hal/cortex-m/imx95/`, built, not yet run on the board · Last verified:
> 2026-11-20
>
> A profile of the board and of what this project builds for it. Its design,
> with every source and every open question, is
> [`../multi-arch/imx95-design.md`](../multi-arch/imx95-design.md) ("the
> design" below); this page cites it or `boards/frdm-imx95-m7/` for each fact
> and adds none of its own. A fact neither gives is marked **to confirm against
> IMX95RM**, the i.MX 95 Reference Manual, which this project does not hold.
> Hardware work on the board is queued after the STM32H573I-DK (#535) and the
> ESP32-P4 (#536), by owner ruling 768.

## Who owns what

Owner ruling 771 (2026-11-17) fixes who owns which part of the i.MX 95:

| Part | Owner | What runs there |
|---|---|---|
| Cortex-A55 secure world | upstream Arm Trusted Firmware-A and OP-TEE, from NXP's BSP | Not code of this project. go-trusted-platform and go-xkms run in Linux on the A55 and reach TrustZone through OP-TEE: the firmware TPM trusted application through the kernel's TPM resource-manager device, the PKCS#11 trusted application through go-xkms's PKCS#11 backend, and the kernel's TEE device for any custom trusted application |
| Cortex-M33 | NXP | The System Manager, NXP firmware |
| EdgeLock Enclave | NXP | The hardware root |
| Cortex-M7 | **this project** | trusted-firmware: the firmware core and the application, board id `0x41`, provisional |

trusted-firmware stays the Cortex-M product, and on the i.MX 95 that is the M7.
The design's section 1 already splits the M7's work three ways (ours,
trusted-imx95's and NXP's); ruling 771 adds the A55 side. The `optee-trustzone`
row is not this board's: it designs a TrustZone-A secure world on QEMU `virt`,
and on the i.MX 95 that world is the BSP's.

## The board

| | | Source |
|---|---|---|
| Board id | `0x41`, **provisional**, recorded in [`board-id-registry.md`](board-id-registry.md) | `boards/frdm-imx95-m7/board_profile.h` (`TFW_BOARD_ID`); the design, question 20 |
| Board | NXP FRDM-IMX95-PRO | UM12527 Rev. 1.0, as the design's sources cite it |
| SoC | i.MX 95, MIMX9596AVZXNAC; its silicon revision is open | UM12527 §1.2 Table 1; the design, question 16 |
| Cores | up to six Cortex-A55, one Cortex-M33, one Cortex-M7; the M7 and M33 are two independent real-time domains | the design, section 2.1 (UM12527 §1.2 Table 1, §2.1) |
| The M7 | up to 800 MHz; 512 kB of TCM with ECC; 32 kB + 32 kB I/D cache, FPU, MPU and NVIC | the design, section 2.1 (IMX95CEC Table 1, Figure 3) |
| Console | the board's M7 console port is LPUART7 on GPIO_IO36/37 at ALT2 (PR #449 bench record, sections 14 and 15; owner ruling 1243, #474, option (a): "LPUART7 for the imx95 M7 console."); the image is still on LPUART3, interrupt 64, until #474's firmware change | the design, sections 3 and 9.4, question 13; interrupt line **to confirm against IMX95RM** |
| Watchdog | WDOG5, WDOG32 on the 32 kHz LPO | the design, section 9.4, question 17; **to confirm against IMX95RM** |
| Ethernet | two RGMII Gigabit ports and a 10 GbE SlimSAS, every PHY on ENET2's MDIO; which the M7 may own is open | the design, section 3 (UM12527 §2.18), question 14 |

## The port

What `boards/frdm-imx95-m7/` and `src/hal/cortex-m/imx95/` set. Every SoC
value is marked TO CONFIRM in the file that states it (the design, section
9.4: "Matched: none. Corrected: none. Unverifiable: all five.").

| | | Source |
|---|---|---|
| Architecture | `CPU := cortex-m7`: the M7 is ARMv7E-M with **no TrustZone-M**, so isolation on it is its MPU and the SoC's resource domain controller (TRDC), which the System Manager configuration sets. IMX95CEC Table 1 says "Arm v8-M supporting Trustzone-M"; the ref ADR's decision 1 says Armv7E-M, which the build follows | `boards/frdm-imx95-m7/profile.mk`; the design, sections 1 and 2.1, question 2 (open) |
| Memory map | ITCM at M7 `0x00000000`: 32 KiB reserved for tfw-boot, the application slot of 192 KiB at `0x00008000`, the platform block in the last 4 KiB at `0x0003F000`; DTCM at `0x20000000`: `.data`, `.bss`, the stack, the reset marker and the handoff block. The A55 sees the ITCM at `0x203C0000` and the DTCM at `0x20400000`. TCM is RAM, so nothing survives a power cycle | `boards/frdm-imx95-m7/profile.mk`; the design, section 9.1; **to confirm against IMX95RM** (question 1) |
| Loaders | the boot container (AHAB, trusted-imx95's) or Linux's remoteproc on the A55; the application carries an empty remoteproc resource table | `profile.mk`; the design, sections 9.1 and 9.2 |
| Clock | `TFW_CM_CPU_HZ` 800 MHz; the LPUART root at 24 MHz | `board_profile.h`; the design, section 9.4; **to confirm against IMX95RM** |
| Clocks, pins, power | none set by us: the System Manager on the M33 owns them, reached over SCMI. The SCMI client is called only by `hsm_port.c`'s A1 and A2 actions, host-tested; not in this board's image, whose `profile.mk` does not set `HSM := 1` (question 21) | the design, sections 9.3 and 9.6 |
| Crypto tier | `BOARD_TIERS := soft` until the EdgeLock Enclave tier lands | `profile.mk`; the design, section 4 |
| Writing the board | nothing is flashed: the image is loaded into TCM | `profile.mk` |

## tfw-boot

Not built for this board. `profile.mk` reserves its 32 KiB at the ITCM's base
(`BOOT_BASE`, `BOOT_BUDGET`), and `mk/boot.mk` has no row for it. Whether the
board gets tfw-boot on the M7 or relies on AHAB alone is the owner's open
question 19 of the design; section 10 of the design says what each answer
changes.

## Security facilities

| | | Source |
|---|---|---|
| TrustZone-M on the M7 | none (ARMv7E-M, above); question 2 open | the design, section 2.1 |
| Isolation of the M7 | its MPU, and the TRDC as the System Manager configuration sets it | the design, sections 1 and 6 |
| Hardware root | the EdgeLock Enclave; NXP's firmware | the design, section 4; ruling 771 |
| Secure boot | AHAB: the ROM and the ELE authenticate the container that holds the M7 image | the design, section 2.2 |
| Key tier | the enclave, `TFW_CRYPTO_SOC_ENCLAVE` (5), proposed; its message layer, session, key store, key management, P-256 key, signature and verify are host-tested on a register model, and not called until questions 3 and 4 are answered | the design, sections 4 and 9.7 to 9.13 |
| Whether the M7 has a route to the enclave | open | the design, question 3 |

## Feature matrix

One line per row of `docs/SPEC.md`'s table, in its order; "SPEC" is the
row's status there. Every row that applies to this board is **not run**: the
board is queued after #535 and #536 (ruling 768), and trusted-imx95 must first
give the M7 its logical machine, console and boot-container slot
(`docs/cortex-m/engineering.md`, Remaining slices, item 5). "Evidence" is what
exists off the board. A row that is not this board's says why.

| Row | SPEC | Status | Date | Evidence | Issue |
|---|---|---|---|---|---|
| `wire-protocol` | shipped | not run | 2026-11-20 | over LPUART3 only; LPUART host-tested on the register model | #49 |
| `device-capabilities` | shipped | not run | 2026-11-20 | GPIO, UART, watchdog and pulse timing (`PULSE_IN`, `PULSE_CAPTURE`, on SysTick) linked by default; PWM on a TPM, the SAR ADC, I²C on an LPI2C and tone on a GPT built only in their opt-in rows (`soft:PWM=1`, `soft:ADC=1`, `soft:I2C=1`, `soft:TONE=1`), host-tested on register models, and answering unsupported until their bases are known (PWM also until the pad handoff, `TFW_IMX95_PWM_PAD_HANDOFF`) (the design, question 17); EEPROM, SPI and 1-Wire not linked | #49 |
| `boards` | shipped | not run | 2026-11-20 | `test/unit/test_board_frdm-imx95-m7.c`; `make board-sweep` | #49 |
| `dice-attestation` | shipped | not run | 2026-11-20 | soft tier only; the enclave tier's message layer built and host-tested (`test/unit/test_imx95_ele_mu.c`), not called until questions 3 and 4 are answered | #49 |
| `secure-boot` | shipped | not run | 2026-11-20 | no tfw-boot row; the boot is open (question 19) | #49 |
| `device-provisioning` | shipped | not run | 2026-11-20 | nothing board-specific built | #49 |
| `host-sdk` | shipped | not this board's | 2026-11-20 | host side; reaches the board over the wire protocol | — |
| `xkms-adapter` | in progress | not this board's | 2026-11-20 | the AVR backend for go-xkms | — |
| `ethernet-transport` | in progress | not this board's | 2026-11-20 | the AVR boards' W5x00 transport; the M7's Ethernet is the design's question 14 | — |
| `cortex-m` | in progress | not run | 2026-11-20 | built in `firmware-matrix`; QEMU has no i.MX 95 (the design, section 9.5) | #161 |
| `stm32h5` | in progress | not this board's | 2026-11-20 | another Cortex-M board | — |
| `secure-element-se05x` | future | not this board's | 2026-11-20 | an element tier; this board's key tier is the enclave | — |
| `optiga-trust-m` | future | not this board's | 2026-11-20 | an element tier; this board's key tier is the enclave | — |
| `rp2350` | in progress | not this board's | 2026-11-20 | another Cortex-M board | — |
| `nrf54l15` | future | not this board's | 2026-11-20 | another Cortex-M board | — |
| `mcxn947` | in progress | not this board's | 2026-11-20 | another Cortex-M board | — |
| `optee-trustzone` | future | not this board's | 2026-11-20 | a TrustZone-A secure world on QEMU `virt`; on the i.MX 95 that world is the BSP's (ruling 771, above) | — |
| `optee-vault-ta` | future | not this board's | 2026-11-20 | runs on `optee-trustzone`'s QEMU `virt` stack | — |
| `esp32` | in progress | not this board's | 2026-11-20 | another architecture port | — |
| `esp32-p4` | in progress | not this board's | 2026-11-20 | another board | — |
| `imx95` | in progress | not run | 2026-11-20 | board as data, SoC HAL, SCMI and enclave messages built and host-tested (the design, section 10) | #49 |
| `hsm-self-defence` | future | not run | 2026-11-20 | the SUIT reader, payload reader and decision engine on the host only (`make host-test`), and the wire commands `0x76` to `0x7A` and `0x7C` (`HSM_RECORD_READ`) in tfwsim-atecc608a (`make sdk-test`); the M7's port, `src/hal/cortex-m/imx95/hsm_port.c`, host-tested (`test_imx95_hsm_port.c`); not linked into this board's image (no `HSM := 1` in its `profile.mk`) | #50 |
| `device-security-event-evidence` | planned | not this board's | 2026-11-20 | carried by `xkms-adapter`, the AVR backend | — |
| `application-sdk` | in progress | not run | 2026-11-20 | applications are board-independent; nothing board-specific built | #49 |
| `ota` | in progress | not run | 2026-11-20 | AHAB plus our trailer v1 and SUIT gate, designed (the design, section 5); not started | #52 |
| `device-onboarding` | future | not run | 2026-11-20 | nothing board-specific; the board supplies DICE evidence through `dice-attestation` | — |

## What this page bears on

- **The `imx95` row's wording.** It does not yet say who owns the A55's secure
  world. The sentence proposed to the owner is in
  [`../plans/537.md`](../plans/537.md); `docs/SPEC.md` is not edited here.
- **The design's question 2.** Ruling 771's brief has the M7 as ARMv7-M with
  no TrustZone-M, which agrees with the ref ADR and the build and not with
  IMX95CEC Table 1.
- **The design's question 20.** The board id stays `0x41`, provisional, until
  the owner allocates it.

## What is not verified

Nothing here has run on the board. Every SoC value (addresses, interrupt
numbers, clocks, pins, the watchdog) is from the files and secondary sources
the design names, not from IMX95RM, and is **to confirm against IMX95RM**. The
first bench slice (the design's slice 8) records the board's factory state
first and reads IMX95RM before it relies on any of them.
