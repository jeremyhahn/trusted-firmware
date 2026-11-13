# i.MX 95 Cortex-M7 board: the design of row `imx95`

> **Status: PROPOSED** · Issue: [#49](../../../../issues/49) · SPEC row: `imx95`
> (in progress) · Scope: trusted-firmware · Last verified: 2026-10-21
>
> #49's first deliverable was the row's design, sections 1 to 8. Section 9 is
> the board as built, and section 10 the row's requirements one by one. SPEC
> rows change at merge (CA m623).

## Sources and how they are cited

Every claim names its source as a vendor document and section, or as a `path`
and section in this repository.

- **NXP, read for this page** (the vendor set indexed by the references
  README of the device):
  - UM12527, *FRDM-IMX95-PRO Board User Manual*, Rev. 1.0, 22 June 2026: §1.2
    Table 1, §2.1, §2.18, §2.19;
  - IMX95CEC, *i.MX 95 Applications Processors Data Sheet for Commercial
    Products*, Rev. 8: Tables 1 and 2, Figure 3, §4.1.5.1, §4.1.9;
  - RM00284, *EdgeLock Enclave Hardware Security Module API*, Rev. 4.5,
    24 September 2026: §1, §2.1–§2.4.2, §3.2, §3.4–§3.8, §3.10, §3.12, §3.21,
    §3.35, §3.37, §3.45, revision history;
  - AN14471, *Using Hardware Security Module for Code Signing*, Rev. 1.0: §1,
    §1.1. It targets the i.MX RT1170 (§1); only its statement of what the CST
    signs is used here;
  - AN14861, *Secure OEM Key Provisioning with EdgeLock Secure Enclave on
    RT1180*, Rev. 1.0: §1, §2. Written for the RT1180; used only to say why
    this design does not import keys.
- **NXP, via OTA ADR d12:** facts checked under #52 and not re-read here
  (UM12527 §2.3 Table 7 boot modes; RM00284 §3.19, §3.24, §3.25) and the public
  SPSDK and U-Boot facts outside the set. They are marked "(d12)".
- **This repository:** `docs/SPEC.md` rows `imx95`, `cortex-m`,
  `hsm-self-defence` and `ota`; [`adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md)
  as decided in `5857be4` on `docs/cortex-m-adr-decisions` (#14, "ref ADR");
  [`adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md) at the
  same commit (#15, "trust ADR"); [`../ota/adr-ota-image-format-and-trust.md`](../ota/adr-ota-image-format-and-trust.md)
  decision 12 and its rejected alternatives ("OTA ADR d12"); the #50 design,
  now [`../hsm-self-defence/architecture.md`](../hsm-self-defence/architecture.md);
  `include/tfw/hal.h`, `include/tfw/crypto.h`, `include/tfw/types.h`,
  `boot/boot_hal.h`.
- **Not read:** issue #161 itself (no forge route from the lane; its branch
  `feat/161-cortex-m-generic` carries no commit past `main`). The generic port
  is taken from the `cortex-m` row and ref ADR decisions 3–5.
- **Missing, never guessed:** the i.MX 95 Reference Manual (IMX95RM), the i.MX
  95 Security Reference Manual (IMX95SRM) with the fuse map, and the System
  Manager interface document. Every fact this design needs from one of them is
  an open question in section 8.

## 1. Scope and non-goals

The row (`docs/SPEC.md`, `imx95`): "The NXP i.MX 95 (FRDM-IMX95-PRO) as a board
of the `cortex-m` port, with our firmware core on its Cortex-M7. The key tier
is the SoC's EdgeLock secure enclave: the DICE root and keys are held there,
and no key leaves it in the clear. AHAB secure boot verifies the M7 image as a
signed container before it runs. [...] trusted-imx95 owns the platform image,
its boot container and the System Manager configuration. This row owns only
the M7 firmware and its board profile, with a board id in the Cortex-M range
(0x40–0x7F)."

| Ours (this row) | trusted-imx95's | NXP's |
|---|---|---|
| The M7 firmware: the core, the protocol, the application hooks | The platform image and its AHAB boot container | The Boot ROM and the ELE firmware |
| `boards/frdm-imx95-m7/board_profile.h` and its board id (proposed `0x41`, ref ADR Q1) | The System Manager configuration: the M7 logical machine, its TCM, pins, LPUART, Ethernet, messaging unit, TRDC | The System Manager (`imx-sm`) on the M33 |
| The SoC files `src/hal/cortex-m/imx95/` on the shared core `src/hal/cortex-m/` (#161) | Signing the container (SRK keys, through go-xkms's custody) | The AHAB format |
| The enclave crypto provider (section 4) and our trailer and SUIT check (section 5) | Staging the container on the boot device | |

**Non-goals.** No tfw-boot: L0 is NXP's ROM, ELE and System Manager (ref ADR
decisions 1 and 8). No code on the M33, which runs the System Manager (IMX95CEC
§4.1.5.1). No DICE ASN.1, certificate profile or verification (go-xkms's), no
enrollment or registry (go-trusted-platform's). The `hsm-self-defence`
enforcement is #50's row; it runs on this port. No algorithm but ES256 (P-256,
SHA-256; owner ruling m522). No format of our own: AHAB is NXP's, the trailer
and SUIT manifest are the `ota` row's, the evidence roles are RFC 9334's.

## 2. The M7, its memory and its boot

### 2.1 What the documents give

| Fact | Source |
|---|---|
| The board carries up to six Cortex-A55, one Cortex-M33 and one Cortex-M7; the M7 and M33 are two independent real-time domains | UM12527 §1.2 Table 1, §2.1 |
| The M7 runs at up to 800 MHz (400 / 667 / 800 MHz by operating mode) | IMX95CEC Table 1, §4.1.9 |
| The M7 has 512 kB of TCM, "sum of instruction and data", with ECC; a 32 kB + 32 kB I/D cache, FPU, MPU and NVIC | IMX95CEC Table 1, Figure 3 |
| The low-power real-time domain block lists a watchdog, a periodic timer, two timer/PWM and two timers; which are the M7's is not stated | IMX95CEC Figure 3; question 17 |
| The M7 sits in M7MIX; the M33 in AONMIX runs the System Manager; in Low Power RUN the A55 is off and the M7 "handles all the computing" | IMX95CEC §4.1.5.1 |
| Ethernet sits in NETCMIX | IMX95CEC Figure 3 and its power-domain figure |
| Memories: 16 GB LPDDR5, 32 GB eMMC 5.1 (uSDHC1, the default boot device), microSD (uSDHC2), 512 Mbit QSPI NOR | UM12527 §1.2 Table 1; boot modes (d12: UM12527 §2.3 Table 7) |
| The M7 image is the optional `cortex_m7_app` in the OEM container of the primary container set, loaded to `0x303C0000`, sharing that container's fuse version | public, outside the set (d12: SPSDK) |
| IMX95CEC Table 1 lists the M7 platform as "Arm v8-M supporting Trustzone-M"; ref ADR decision 1 has it as Armv7E-M with no Security Extension | IMX95CEC Table 1; open question 2 |

**What no document in hand gives:** the M7's TCM addresses and the ITCM/DTCM
split as the M7 sees them, whether `0x303C0000` is that TCM, and how much of it
the System Manager configuration leaves the M7 (questions 1 and 18).

### 2.2 The boot chain

```
power-on
  └─ Boot ROM + ELE ............ NXP: AHAB authenticates the container
       └─ System Manager (M33) .. NXP code, configured by trusted-imx95;
                                 starts the M7 logical machine, image in TCM
            └─ tfw firmware (M7) . ours: core + application, no tfw-boot
```

This is ref ADR decision 8. The CST signs AHAB containers, and its back end can
be a PKCS#11 HSM (AN14471 §1.1); the SRK keys therefore sit in go-xkms's
custody, through trusted-imx95's signing, never here.

### 2.3 Where our image goes

One image: the core and the application, linked for the M7's TCM, with
trailer v1 and the SUIT manifest appended inside the span AHAB signs (section
5). There is no tfw-boot slot, no handoff block and no boot section. The
budgets stay ref ADR Q2's 128 KiB code and 32 KiB RAM until question 18 is
answered; the linker script takes the TCM origin and lengths from the board
profile, so an answer is a data change.

## 3. The HAL, function by function

"Core" is `src/hal/cortex-m/` (#161); "SoC" is `src/hal/cortex-m/imx95/`.
The table extends ref ADR decision 4 with the functions `include/tfw/hal.h`
has gained since (`tfw_hal_flash_read`, the watchdog).

| `hal.h` function | Where | On `frdm-imx95-m7` | Rests on |
|---|---|---|---|
| `tfw_hal_init` | core + SoC | SysTick; LPUART; clocks and pins requested from the System Manager over SCMI | ref ADR d4; question 15 |
| `tfw_hal_reset_cause` | SoC | the System Manager's reset reason for the M7 logical machine, mapped onto `tfw_reset_cause_t` (`SOFTWARE` 5 and `PANIC` 6 now exist, `include/tfw/types.h`); an unmapped reason reads `UNKNOWN` | question 15 |
| `tfw_hal_reset` | core | an SCMI logical-machine reset request; what it resets is System Manager policy | ref ADR d4 |
| `tfw_hal_irq_enable` | core | `cpsie i` | ref ADR d4 |
| `tfw_hal_millis` | core | SysTick at 1 kHz | ref ADR d4 |
| `tfw_hal_delay_us` | core | busy-poll of SysTick `VAL`, for bus bit-banging only | ref ADR d4 |
| `tfw_hal_uart_init`, `_rx`, `_tx_try`, `_tx_idle` | SoC | the LPUART the System Manager gives the M7; on the board, one of the four CH9114F channels on J22, whose core mapping is not fixed | UM12527 §2.19; question 13 |
| `tfw_hal_gpio_mode`, `_write`, `_read` | SoC | RGPIO; pin mux from the System Manager | ref ADR d4 |
| `tfw_hal_adc_init`, `_set_ref`, `_read` | SoC | the 12-bit SAR ADC (`sar_adc.h`), polled, in the `soft:ADC=1` row only: calibrated once at init, one-shot normal conversion per read, reference VCC only; base `TFW_IMX95_ADC_BASE` zero (unsupported) until a pinned source gives it. Built and host-tested on a register model, not run | Linux `imx93_adc.c`; questions 13 and 17 |
| `tfw_hal_pwm_write`, `_stop` | SoC | a TPM channel, edge-aligned (`hal_pwm.c`, `tpm.h`); `soft:PWM=1` row only; built and host-tested on a register model, not run; base zero until question 17 (section 9.5) | Linux v6.12 `pwm-imx-tpm.c`, `imx95.dtsi`; question 17 |
| `tfw_hal_tone_start`, `_stop` | `hal_tone.c`, `gpt.h` | built (soft:TONE=1 row), host-tested, not run: GPT output compare 1 in restart mode, its interrupt toggling the pin through `tfw_hal_gpio_write`; base zero (unsupported), 24 MHz clock and vector `tfw_cm_irq0` TO CONFIRM, question 17 | Linux `drivers/clocksource/timer-imx-gpt.c` |
| `tfw_hal_pulse_in`, `tfw_hal_pulse_capture` | — | not linked | — |
| `tfw_hal_eeprom_read`, `_write` | — | not linked; `TFW_EEPROM_SIZE 0` | ref ADR d4 |
| `tfw_hal_nvm_read` | SoC | the platform block; where it lives is open | ref ADR Q4; question 6 |
| `tfw_hal_flash_read` | SoC | a copy from the running image in TCM (the trailer and SUIT reader read the image through it) | section 2.3 |
| `tfw_hal_handoff_block` | — | not linked: no tfw-boot, so no handoff | ref ADR d4 |
| `tfw_hal_i2c_init`, `_xfer` | SoC | one LPI2C instance as a polled master (`hal_bus.c`, registers in `lpi2c.h`), built only with `I2C=1` (`profile.mk`, the `soft:I2C=1` row); tested on the host against a register model (section 9.5); while `TFW_IMX95_LPI2C_BASE` is zero every call answers `TFW_ERR_UNSUPPORTED` and touches no register; not yet run on a board | ref ADR d4; questions 13, 17 |
| `tfw_hal_spi_init`, `_xfer` | SoC | not on this board: `TFW_ERR_UNSUPPORTED` (LPSPI later) | ref ADR d4 |
| `tfw_hal_ow_reset`, `_write`, `_read` | SoC | not on this board: `TFW_ERR_UNSUPPORTED` | — |
| `tfw_hal_free_ram` | core | the stack pointer less the end of `.bss` | ref ADR d4 |
| `tfw_hal_watchdog_arm`, `_pet` | SoC | a watchdog the System Manager assigns the M7 | IMX95CEC Figure 3; question 17 |

**The Ethernet path** (`include/tfw/eth.h`: `tfw_eth_start`, `_send`,
`_close`) is not a HAL function; today it is the W5x00 driver. On this board
it is one of the two RGMII Gigabit ports (YT8521SH-CA PHYs U54 and U56 to J13
and J14) or the 10 GbE SlimSAS J12, and every PHY shares ENET2's MDIO/MDC
(UM12527 §2.18). Which controller the M7 may own, and whose driver, are
questions 14 and 15. Until then the M7 speaks the protocol over its LPUART only.

**The boot HAL** (`boot/boot_hal.h`) is not linked on this board: with no
tfw-boot, every `tfw_boot_*` function and every `boot/` object leaves the
object list. The QEMU board `mps2-an505` (#161) keeps tfw-boot for the core.

## 4. The key tier: the EdgeLock enclave

The tier is `TFW_CRYPTO_SOC_ENCLAVE` (5), proposed by trust ADR D3, added past
`TFW_CRYPTO_SECURE_ELEMENT` (2, `include/tfw/types.h`) in its own extend-only
PR with its vector and Go mirror. The provider is a new `src/crypto/ele/`,
selected by the object list. It speaks RM00284's services through a session:
"The session establishes a route (MU, DomainID...) between the requester and
the HSM" (RM00284 §2.1). Whether the M7 has such a route is question 3; every
row below waits on it.

| `crypto.h` function | RM00284 service | Notes |
|---|---|---|
| `tfw_crypto_init` | `hsm_open_session` (§3.1), then the key store service (§2.3, §3.22) | at most 8 sessions and 32 services (§3.1) |
| `tfw_crypto_tier` | — | 5 |
| `tfw_crypto_algorithms` | — | ES256 only (m522) |
| `tfw_crypto_random` | `hsm_get_random` / `hsm_do_rng` (§3.6) | the enclave's TRNG |
| `tfw_crypto_sha256` | — | `src/crypto/sha256.c`: the input is public, so no round trip to the enclave |
| `tfw_crypto_hmac` | `hsm_mac_one_go` (§3.10) under an `HSM_KEY_TYPE_HMAC` key permitted `HMAC_SHA256` (§3.2) | the DICE one-way function with the UDS as a key generated in the enclave; which measurement it takes is question 5 |
| `tfw_crypto_sign` | signature generation, `HSM_SIGNATURE_SCHEME_ECDSA_SHA256` (§3.4), under an `HSM_KEY_TYPE_ECC_NIST` key of 256 bits permitted `ECDSA_SHA256` (§3.2) | the DeviceID key, generated in the enclave |
| `tfw_crypto_pubkey` | exported at generation, or `hsm_pub_key_recovery` (§2.4, §3.21) | only symmetric and private keys are stored (§2.4) |
| `tfw_crypto_counter` | none | RM00284 offers no general monotonic counter to software (d12, by absence across §3); `TFW_ERR_UNSUPPORTED` |
| `tfw_crypto_verify_operator` | `hsm_verify_signature` (§3.5) with the key passed in the call (§3.5.2.2) | signatures as `r‖s`, uncompressed keys only (§3.5) |
| `tfw_crypto_verify_image` | `hsm_verify_signature` (§3.5) under the board id's image key | the key is "provisioned", never named by the caller (`crypto.h`); where the pin lives is question 6 |

**The private key never leaves the enclave.** "Secret keys cannot be exported
under any circumstances, while they can be imported in encrypted form" (RM00284
§2.4). The DeviceID and the UDS are generated inside the enclave, never
imported: AN14861 §2's import flow (ECDH with NXP's manufacturing key, a wrapped
blob) is for keys made outside, and a key made outside has been outside.

**What the documents constrain.**

- **Durability.** A key store reaches the external NVM only after an operation
  flagged `STRICT OPERATION`; all else is lost at reset, and the monotonic
  counter that protects those exports is limited, so updates are batched
  (§2.4, §2.4.2). Generation of the DeviceID and UDS is one strict operation.
  Where that NVM is for an M7 with no file system is question 4: the data
  storage service keeps its blobs in "the Linux filesystem" (§3.8.5.1).
- **Generic crypto is not supported on the i.MX 95** (§3.45; removed on B0 in
  RM00284 v4.0, revision history). The provider uses only the dedicated
  services above. The board's silicon revision is question 16.
- **Key derivation** is HKDF-SHA256 inside the key store, from a stored key, to
  a volatile derived key (§3.37). Whether it can yield a P-256 key, and what
  measurement it can take, is question 5. Until it is answered the enclave row
  keeps the DeviceID as the alias subject (trust ADR D4, "Element and enclave
  rows").

**Attestation.** `hsm_dev_attest` (§3.12) returns, under a caller's nonce, the
SoC id and revision, the lifecycle, the chip UID, the hash of the installed
enclave firmware, the OEM SRK hash and its revocation status, and a signature.
In RFC 9334 terms the enclave's report is evidence about the platform below us,
endorsed by NXP; our DICE evidence, signed by the DeviceID, is evidence about
the M7 image. Both go to the verifier unchanged; how go-xkms appraises the
enclave's report is go-xkms's (section 6). Whether the M7's session may call
it is question 3.

## 5. OTA on this board

The composed model (#79): the vendor's signature and ours, two keys, two
owners. "The vendor's signature only, on composed platforms" is rejected,
because "our trailer rides beside it" (OTA ADR, rejected alternatives); the
`ota` row says the same.

1. **AHAB (NXP's, trusted-imx95's key).** The ROM and the ELE authenticate the
   container before the M7 starts (section 2.2).
2. **Our ES256 gate (the row key).** Trailer v1 with board id `0x41` and the
   SUIT-subset manifest (ES256, SHA-256), appended to the M7 image before the
   container is signed, so both lie inside the AHAB-signed span, as on the
   ESP32-P4 (`esp32-p4-design.md` §2.2).

**Where our gate runs.** There is no tfw-boot here. The running, AHAB-verified
M7 firmware checks an incoming image before it hands it to the stager, with the
primitives tfw-boot uses on main: `tfw_boot_trailer_parse`,
`tfw_boot_binding_permit`, `tfw_boot_rollback_permit` (`boot/boot_image.c`),
`tfw_suit_gate` (`src/core/suit.c`), and the signature by
`tfw_crypto_verify_image` in the enclave. The `OTA_ACTIVATE` authorization, a
COSE_Sign1 ES256 over the action, measurement and version (the `ota` row), is
checked at the same point, not at the next reset. Whether this install-time
check meets m453's "verify on every row" for a row with no tfw-boot is question 12.

**The anti-rollback floor.** The enclave holds exactly one monotonic value that
bears on the M7 image: the OEM container's fuse version, burned by `hsm_commit`
with `HSM_COMMIT_OEM_FW_VERSION` (RM00284 §3.35). It is shared with the System
Manager and U-Boot SPL in that container (d12, SPSDK), it is a fuse with few
steps (question 7), and only the boot ELE firmware offers the commit (d12),
so whether the M7 may call it is question 9. The brief's "floor in an enclave
counter" has no other home in RM00284: the monotonic counter guards key-store
exports only (§2.4.2), and data storage claims no roll-back protection (§3.8,
d12). The per-release floor that the `ota` row advances at confirm therefore
has no store on the documents in hand (question 8). Following ref ADR Q5, there
is no M7 OTA command until questions 8, 10 and 11 are answered.

**Staging and trial.** OTA ADR d12 recommends the A-core agent stage into the
inactive container slot, with the ROM's primary/secondary fallback as the trial
and the boot pointer flipped after the control plane's confirm. What triggers
the fallback, and who stages, are questions 10 and 11.

## 6. Seams

| Seam | Other side | What crosses | Ours |
|---|---|---|---|
| Key tier and attestation | go-xkms | The DeviceID public key and our DICE evidence; the enclave's `hsm_dev_attest` report to appraise; the DICE certificate profile; the signing service that signs the trailer, the SUIT manifest and the `OTA_ACTIVATE` authorization with the row key | `imx95` (the provider), `ota` |
| AHAB signing keys | go-xkms (custody, PKCS#11), through trusted-imx95 | SRK signatures over the container (AN14471 §1.1); no key here | none |
| Enrollment and OTA control | go-trusted-platform | Device enrollment and registry; its `device-ota` seam; the confirm after appraisal | `ota` |
| Container and System Manager configuration | trusted-imx95 | The M7 logical machine, its TCM, LPUART, pins, Ethernet, messaging unit, TRDC, watchdog; the container slot the stager writes | `imx95` |
| ROM, ELE firmware, System Manager | NXP | AHAB; RM00284's services; SCMI | `imx95` |
| The M7 enforcement | this repository | The port this row provides | `hsm-self-defence` (#50) |

## 7. Slices

Each is one PR. **Gates:** every slice waits on #7, the AVR end-to-end gate, as
the #50 design's slices do, unless the owner lifts it for this row as it was
lifted for `esp32-p4` (`adr-esp-idf-integration.md`, header) and the Cortex-M
ADRs (CA m623); the hardware order is SE052F, then the
i.MX 95 M7, then the STM32H5 (`cortex-m` row, ruling 269).

| # | Slice | Files | Test | Gate |
|---|---|---|---|---|
| 1 | **Board profile and image layout.** | `boards/frdm-imx95-m7/board_profile.h`, its linker script, the board id `0x41` (extend-only), `docs/boards/frdm-imx95-m7.md` | The cross build links for the M7 in CI; `make ci` | #161 merged; questions 1, 2, 18 |
| 2 | **SCMI client.** | `src/hal/cortex-m/imx95/` (base, clock, pin, reset, logical machine); the mock; SCMI's title and URL in `docs/reference/` | Host unit against recorded SCMI messages | slice 1; question 15 |
| 3 | **Console, GPIO, time, watchdog.** | `src/hal/cortex-m/imx95/` LPUART, RGPIO, watchdog | Host unit on a register model; then the protocol over the M7's J22 channel on the bench host | slice 2; questions 13, 17; trusted-imx95's M7 logical machine |
| 4 | **Tier value 5.** | `include/tfw/types.h`, `testdata/vectors.h`, the Go mirror | The literal vector; an older host reads 5 as unrecognised | none beyond #7 |
| 5 | **The enclave provider.** | `src/crypto/ele/`; its messaging-unit transport | Host unit against a model of the messaging unit; the board with throwaway keys only | slices 3, 4; questions 3, 4, 6, 16 |
| 6 | **Our OTA gate on the M7.** | the receive path calling `boot/boot_image.c` and `src/core/suit.c` | Host unit: a good image passes; another board's, a lower version, a bad SUIT and a bad authorization are refused | slice 5; questions 8–12 |
| 7 | **Ethernet.** | the M7's controller behind `include/tfw/eth.h` | Host unit; then the CLI end to end over TCP on the bench host | slice 3; question 14 |
| 8 | **Bench.** | the board page | End to end on the bench host's FRDM-IMX95-PRO; factory state recorded first (m346); any fuse write an owner decision | slices 3–6; trusted-imx95's container |

Slices 1, 2 and 4 need no board.

## 8. Open questions

Each names the document that answers it; none is guessed here.

1. **The M7's TCM map.** The ITCM and DTCM addresses and split as the M7 sees
   them, and whether `0x303C0000` (SPSDK) is that TCM. IMX95RM, memory map.
2. **The M7's architecture.** IMX95CEC Table 1 says "Arm v8-M supporting
   Trustzone-M"; ref ADR decision 1 says Armv7E-M. Which, for the compiler
   flags and the MPU? IMX95RM, the Arm core chapter.
3. **A route from the M7 to the enclave.** Does the M7 have a messaging unit to
   the ELE, under which domain ID, and may it open a session and call
   `hsm_dev_attest`? IMX95RM (MU chapter), IMX95SRM. The message layer is
   built against the ELE MU's registers as Linux gives them (section 9.7);
   that the M7 can reach such an MU at all is what stays open.
4. **Where the M7's key store lives.** RM00284 §2.4.2 exports it to "external
   NVM"; on Linux that is a file system. For an M7 running without Linux, what
   holds it across reset? IMX95SRM, the System Manager interface document.
5. **The CDI source.** Can the enclave take a measurement of the M7 image made
   below the M7 (ROM, ELE or System Manager) into a derivation, and can §3.37's
   HKDF yield a P-256 key? IMX95SRM, key-derivation chapter.
6. **The image key pin and the platform block.** Is the row's image key a value
   inside the AHAB-signed container, an imported public key the enclave keeps,
   or fuse data? IMX95RM (AHAB chapter), IMX95SRM; the owner (ref ADR Q4).
7. **The fuse version.** Which fuse word backs the OEM container's fuse
   version, how many steps it has, and how many monotonic-counter fuse words
   the i.MX 95 has. IMX95SRM, the fuse map.
8. **The per-release floor.** With no general counter in RM00284 and no
   roll-back claim on data storage, where does the floor advanced at confirm
   live? The owner, with IMX95SRM.
9. **The commit.** May the M7's session call `hsm_commit` for
   `HSM_COMMIT_OEM_FW_VERSION`, or only another core? IMX95SRM.
10. **The trial.** What triggers the ROM's secondary boot, which fuse sets its
    offset, can software force one secondary boot, and does the ROM boot the
    enabled eMMC partition first? IMX95RM, System Boot chapter.
11. **The stager.** Does the A-core agent write the inactive container slot, or
    does the M7? The owner and trusted-imx95 (trust ADR Q5).
12. **The verify ruling.** With no tfw-boot on this row, is the install-time
    check of section 5 the signature verify m453 requires? The owner.
13. **The console.** Which LPUART and pins the System Manager gives the M7, and
    which J22 channel that is (UM12527 §2.19: not fixed). trusted-imx95.
14. **Ethernet.** Which controller and port the M7 may own, given the shared
    ENET2 MDIO (UM12527 §2.18), and may we compose NXP's driver for it?
    IMX95RM (NETC chapter), the System Manager interface document; the owner
    for the licence.
15. **SCMI.** Which protocols and resources the M7's agent may use, and the
    encoding of its reset reason. The System Manager interface document.
16. **The silicon revision.** The board carries MIMX9596AVZXNAC (UM12527 §1.2
    Table 1). IMX95CEC Table 2 lists only the `MIMX9596D…` parts, all silicon
    revision B0, and RM00284 v4.0 changed the enclave API on B0. Which
    revision is the board's part? The data sheet that covers it, or the chip.
17. **The watchdog.** Which watchdog the M7 owns, and whether its expiry resets
    the M7 logical machine only. IMX95RM, the System Manager interface document.
18. **The budgets.** 128 KiB code and 32 KiB RAM against the TCM the System
    Manager configuration gives the M7. trusted-imx95; IMX95RM.
19. **The boot choice: tfw-boot on the M7, or AHAB alone.** The owner's. This
    is a proposal with the facts for each side; neither is built.
    - *AHAB alone* (section 2.2, ref ADR decision 8). The ROM and the ELE
      authenticate the container that holds the M7 image, and the System
      Manager starts the M7, so no code of ours runs below the application.
      The application would get tfw-boot's 32 KiB of ITCM (question 18). The
      M7 starts with no Linux, as the ref ADR requires. Against it: the
      signature verify m453 requires on every row would be AHAB's, not ours
      (question 12). The CDI would have to come from the enclave (question 5),
      not from tfw-boot's soft path as on every other board. And OTA's trial
      boot would be the ROM's (question 10), not tfw-boot's.
    - *tfw-boot on the M7*. Slice 1 already reserves its 32 KiB at the ITCM
      base, where the profile assumes the M7's reset vector points (TO
      CONFIRM). The profile and `board_profile.h` also lay out its handoff
      block in the DTCM and its soft UDS in its region, so section 2.3's "no
      tfw-boot slot, no handoff block" no longer matches the build. tfw-boot
      would verify trailer v1 and the SUIT manifest with the ES256 code every
      other board uses, and would derive the soft DeviceID as it does there
      (#48, #130, #173). Against it: the TCM is RAM, so a loader writes
      tfw-boot in again at every power-on, and its integrity is still AHAB's,
      or nobody's when Linux loads it. A UDS carried in a loaded image is as
      secret as that image's storage.
    - *The loader*. The board carries the remoteproc resource table
      (section 9.2, met in section 10) so that Linux can load and start
      the M7. The ref ADR
      says the M7 is started "by the ROM and the SM, never by Linux
      `remoteproc`". The kernel's ELF loader checks the table's form, not a
      signature. Is remoteproc a supported loader, a development-only one, or
      none? Under AHAB alone, an image Linux loads is verified by nothing.
20. **The board id.** `boards/frdm-imx95-m7/board_profile.h` keeps `0x41`,
    provisional. Four row branches now claim `0x41`: stm32h5, nrf54l15, rp2350
    and imx95. The owner allocates board ids (ref ADR Q3); each branch takes
    its id from that allocation before it merges.
21. **The M7 agent's SCMI channel.** The SCMI client (section 9.6) names no
    instance, because no document held here gives one. It needs: the MU
    instance and its base on the M7's map; the doorbell (the GCR bit); the
    shared memory area's address and length; the agent id; the SCMI clock id
    of LPUART3's root; and the power, reset and logical-machine ids the M7 may
    use (question 15). So the console's clock is not enabled through SCMI yet.
    The System Manager configuration (trusted-imx95) and the System Manager
    interface document answer the ids. IMX95RM, the MU chapter, answers the
    GCR's offset, `0x114` in `scmi_mu.h`. It is unconfirmed: no document
    this project holds gives it. Linux v6.12 `drivers/mailbox/imx-mailbox.c`
    agrees, as a secondary source. There `fsl,imx95-mu` is an MU v2 whose
    control registers are `{ 0x8, 0x110, 0x114, 0x120, 0x128 }`, and the
    third is the GCR.

## 9. The board as built (frdm-imx95-m7)

The i.MX 95's Cortex-M7 is the `cortex-m` port's second board, after the QEMU
board `mps2-an505`. `boards/frdm-imx95-m7/` takes the next Cortex-M id,
`0x41`, provisional as `0x40` is (`../boards/board-id-registry.md`). The board
is in NXP UM12527, "FRDM-IMX95-PRO Board User Manual", Rev. 1.0. Its SoC is in
NXP IMX95RM, the "i.MX 95 Applications Processor Reference Manual", which this
project does not hold. Every address here is therefore to be confirmed against
IMX95RM (`boards/frdm-imx95-m7/profile.mk`). The A55 addresses are those in
Linux's `drivers/remoteproc/imx_rproc.c`.

### 9.1 Memory map

| Region | M7 base | A55 base | Size | Holds |
|---|---|---|---|---|
| ITCM | `0x00000000` | `0x203C0000` | 32 KiB | reserved for tfw-boot, which has no row for this board yet |
| ITCM | `0x00008000` | `0x203C8000` | 192 KiB slot | the application |
| ITCM top | `0x0003F000` | `0x203FF000` | 4 KiB | the platform block |
| DTCM | `0x20000000` | `0x20400000` | 256 KiB | `.data`, `.bss`, the stack, the reset marker, the handoff block |

The image is linked at the M7's addresses. Two loaders can write it into the
TCM from the A55 side: the boot container (AHAB) or Linux's remoteproc.
`mk/arch/cortex-m/imx95.ld` takes `image.ld`'s layout as it is. TCM is RAM,
so nothing survives a power cycle.

### 9.2 The remoteproc resource table

For remoteproc, the application carries a resource table in the format of the
kernel's *Remote Processor Framework*, "Binary Firmware Structure"
([`../reference/README.md`](../reference/README.md)). The kernel's ELF loader
finds the table by its section name, `.resource_table`, accepts version 1 only,
and copies the table back to the section's address after loading.

- `src/hal/cortex-m/imx95/rsc_table.c` holds the 16-byte header: `ver` 1,
  `num` 0, both reserved words zero.
- The table has no entry. The image runs from the TCM, which the i.MX driver
  maps itself, so it needs no carveout or devmem. It writes no log, so it has
  no trace buffer. It speaks no rpmsg, so it has no vdev.
- `image.ld` places the section after `.ARM.exidx`, in the code region and
  before `.data`'s initial values, and drops the empty section on the other SoCs.
  Placed from `imx95.ld`, after the shared script's last section, GNU ld would
  warn "dot moved backwards" on every link.
- `imx95.ld` asserts the table's length and alignment.
- `test/unit/test_imx95_rsc_table.c` checks the bytes against literals written
  out from the format.
- tfw-boot does not link the file, so its image has no table.

The table does not settle who starts the M7; the ref ADR says Linux never does
(section 8, question 19).

### 9.3 The SoC directory

`src/hal/cortex-m/imx95/` drives three blocks:

- LPUART3, on interrupt 64, as the byte link;
- the RGPIO banks GPIO1 to GPIO5;
- WDOG5, as the watchdog both images share.

It sets no pin mux, clock or power: NXP's System Manager on the M33 owns those.
The divisor search and the bank table are `soc_calc.c`, which has no register
access, so `test/unit/test_imx95_soc_calc.c` tests on the host the same file
the image links. The vector table carries 80 external interrupts for LPUART3,
so the code region needs a 512-byte alignment, and `imx95.ld` asserts it.

### 9.4 SoC facts, checked 2026-10-20

Slice 1's SoC facts, checked against UM12527 and RM00284 as far as
`docs/reference/` holds them. It holds neither, and RM00284 is the EdgeLock
Enclave's API reference, which does not describe these blocks. So no fact
could be shown wrong and none was changed. Each stays marked "TO CONFIRM" in
the file that states it.

| Fact | Where | Value | Result |
|---|---|---|---|
| The M7's UART | `board_profile.h`, `soc_regs.h` | LPUART3 at `0x42570000`, interrupt 64 | unverifiable. UM12527 §2.19 says J22's channel-to-core mapping is not fixed (question 13) |
| The watchdog | `soc_regs.h`, `hal_soc.c` | WDOG5 at `0x424B0000`, WDOG32 on the 32 kHz LPO | unverifiable (question 17) |
| The start address | `profile.mk` | ITCM at M7 `0x00000000` = A55 `0x203C0000`, application at `0x00008000` | unverifiable. SPSDK loads `cortex_m7_app` at `0x303C0000` (d12); whether that is the other alias of the same TCM is open |
| The clock | `board_profile.h` | the M7 at 800 MHz, the LPUART root at 24 MHz | unverifiable. 800 MHz agrees with IMX95CEC Table 1 as section 2.1 cites it, a datasheet `docs/reference/` does not hold |
| The pins | `board.c`, `soc_calc.c` | GPIO2_IO00 and GPIO2_IO01; GPIO1 to GPIO5 at `0x47400000`, `0x43810000`, `0x43820000`, `0x43840000`, `0x43850000` | unverifiable |

Matched: none. Corrected: none. Unverifiable: all five.

### 9.5 What the board does not do yet

`firmware-matrix` builds it, but nothing runs it, because QEMU has no i.MX 95.
Its UART, pins and watchdog run on the host against a register model
(`test/model/imx95_model.c`, through `test/unit/test_imx95_hal.c`): the model
pins the register layouts soc_regs.h states, so it proves the code agrees with
them, not that they are the silicon's (section 9.4).
Its SAR ADC, in the `soft:ADC=1` row, runs the same way against the register
model in `test/unit/test_imx95_adc.c`; its register layout is NXP's i.MX 95
device header `PERI_ADC.h`, cross-checked against Linux
`drivers/iio/adc/imx93_adc.c` at v6.12, both pinned in
[`../reference/README.md`](../reference/README.md) (IMX95RM is not held, so the
header is the authority). The base is zero (unsupported) until a pinned source
gives the M7's instance, so the firmware's analog read answers unsupported on
the board.

I2C is optional: `I2C=1` (`profile.mk`, built by the `soft:I2C=1` row) links
the LPI2C master in `src/hal/cortex-m/imx95/hal_bus.c`. It is built and
host-tested, never run on a board.

- `test/unit/test_imx95_lpi2c.c` drives it against a register model of one
  LPI2C instance: the command and receive FIFOs and the NDF, ALF, FEF, SDF
  and EPF flags. The model's offsets and bits are written out from Linux's
  `i2c-imx-lpi2c.c`, TO CONFIRM against IMX95RM.
- The cases: writes, reads and a write then read on a repeated START; an
  address NACK and a data NACK, early, on the last byte and seen only with
  the STOP (`TFW_ERR_NACK`); lost arbitration on a data byte and at the STOP
  (`TFW_ERR_BUSY`, no STOP of ours queued after ALF); a bus that never moves
  (`TFW_ERR_TIMEOUT`); and the clock divisor at 24 and 16 MHz.
- The instance is open (questions 13 and 17), so `TFW_IMX95_LPI2C_BASE` is
  zero in `board_profile.h`. At that base `tfw_hal_i2c_init` and
  `tfw_hal_i2c_xfer` answer `TFW_ERR_UNSUPPORTED` and touch no register; the
  test checks that refusal.

PWM (#22) is built and host-tested, not run: `src/hal/cortex-m/imx95/hal_pwm.c`
drives edge-aligned PWM at 1 kHz on a TPM channel (`tpm.h`: the layout from
Linux v6.12 `drivers/pwm/pwm-imx-tpm.c`, the six instances' bases from Linux
v6.12 `imx95.dtsi`, `docs/reference/`), polled, with no pin mux and no clock
set (the System Manager's). Only the `soft:PWM=1` row builds it, with
GPIO2_IO02 and GPIO2_IO03 (PROVISIONAL) on channels 0 and 1: pins 2 and 3, or
4 and 5 in the `soft:ADC=1` row, after the ADC inputs' pins 2 and 3. Its tests
(`test/unit/test_imx95_tpm.c`) run against a TPM model of the test file's
own. Which TPM is the M7's is question 17, so `TFW_IMX95_TPM_BASE` is zero and
every PWM call answers `TFW_ERR_UNSUPPORTED`, touching no register; the TPM's
24 MHz root (`TFW_IMX95_TPM_CLK_HZ`), the pins' pads on UM12527's expansion
header and the TPM's STATUS and CONF offsets are TO CONFIRM.

It has no tfw-boot row and no EdgeLock enclave tier yet. It has the enclave's
message layer and one command (section 9.7), and no key store, session or
attestation over it.

### 9.6 The SCMI client

The M7 asks NXP's System Manager for clocks, power and resets in Arm DEN0056's
SCMI ([`../reference/README.md`](../reference/README.md)).

- `src/hal/cortex-m/imx95/scmi.c` is pure: it has no register access.
  - It encodes the header: message id, type, protocol id, token.
  - It encodes the Base protocol's PROTOCOL_VERSION and PROTOCOL_ATTRIBUTES,
    the Clock protocol's CLOCK_CONFIG_SET and CLOCK_RATE_GET, the Power domain
    protocol's POWER_STATE_SET and the Reset domain protocol's RESET.
  - It refuses a reply whose header is not the command's.
  - It returns the status code as the specification numbers it.
  - CLOCK_CONFIG_SET carries `extended_config_val` only from Clock
    protocol version 3.0, so the caller passes the version that
    PROTOCOL_VERSION returned.
  - The seventh working slice of the row (its own numbering, not section 7's
    slice 7) checked every value against DEN0056E (version 3.2, issue E).
    - All of them match: the header and protocol ids (§4.1.2, Tables 2 and
      3), PROTOCOL_VERSION and PROTOCOL_ATTRIBUTES (§4.1.3, Table 4), the
      status codes (§4.1.4, Table 5), the Base attributes (§4.2.2.3),
      POWER_STATE_SET and its states (§4.3.2.6, §4.3.1 Table 6), the clock
      commands (§4.6.2.8, §4.6.2.9 Table 16), RESET and its cold state
      (§4.8.2.6, §4.8.1 Tables 20 and 21), and the transport (§5.1.1, §5.1.2
      Tables 25 and 26).
    - One name was corrected: the third word was called `oem_config_val`,
      and DEN0056E §4.6.2.9 names it `extended_config_val`. The bytes do not
      change.
    - The layout before Clock version 3.0 (bit 0 alone, two words) is the
      earlier revisions', whose text is not held, so it is not checked.
- `src/hal/cortex-m/imx95/scmi_mu.c` is the transport: the shared memory area's
  layout with an MU general-purpose interrupt as the doorbell.
  - It polls, reading the channel status at most 100000 times before and
    after the send.
  - It never asks for the completion interrupt, and nothing sleeps.
  - It refuses a message, sent or received, that is longer than the area
    or the buffer.
- The channel (MU, doorbell, area) is the caller's data, and nothing calls the
  client yet (question 21).
- `test/unit/test_imx95_scmi.c` pins the bytes as literals.
- `test/unit/test_imx95_scmi_mu.c` runs the channel against the register
  model's MU and shared memory, with a System Manager written in the test.

### 9.7 The enclave's message layer

The first piece of section 7's slice 5: the M7's half of the EdgeLock
Enclave's messaging, and one command. It lives beside the SCMI transport in
`src/hal/cortex-m/imx95/ele_mu.c`, because it is the MU's; the provider in
`src/crypto/ele/` will call it.

- **What RM00284 confirmed.** Nothing on the wire. RM00284 Rev. 4.5 is the
  library's API: it names no header, status word, command id or MU
  register. §1 names the reference implementation, nxp-imx/imx-secure-enclave;
  §3.13.2.2 the lifecycle field; §3.13.3.1 `hsm_dev_getinfo` and §3.19.3.1
  `hsm_get_info` are the library calls over the message built here.
- **Where the values come from** ([`../reference/README.md`](../reference/README.md)):
  - The header word is version, size in words, command and tag, a byte each
    from bit 0: imx-secure-enclave d7e1180b `sab_msg_def.h` struct
    sab_mu_hdr, and U-Boot ece349ad `ele_api.h` struct ele_msg.
    - The version is 0x06 in both.
    - The tags (command 0x17, response 0xE1) are U-Boot's alone.
  - The status word is the status in bits 7 to 0 (0xD6 success, 0x29
    failure) and the rating in bits 15 to 8.
  - GET_INFO is command 0xDA.
    - The request is four words: the buffer's address, high word first, and
      its length, 256.
    - The response is two words, the header and the status.
    - The enclave writes `struct dev_info` and `struct dev_addn_info` into
      the buffer (`sab_dev_getinfo.h`; U-Boot `ele_api.c` ele_get_info).
  - The MU registers (PAR +0x004, TSR +0x124, RSR +0x12C, TR +0x200, RR
    +0x280) and the word order (word k through register k modulo the count
    PAR gives) are Linux v6.12 `imx-mailbox.c`'s for `fsl,imx95-mu-ele` and
    U-Boot's `ele_mu.c`. They are unconfirmed against IMX95RM.
- **Why GET_INFO.** It is the one command the enclave answers with no key or
  session open: its message carries no session handle, and U-Boot sends it
  before any.
  - The ping (0x01) is also session-free, but no source held gives its
    sizes.
- **What was built.** `tfw_ele_call` sends one request and reads one
  response.
  - It polls each status register at most 100000 times per word, and
    nothing sleeps.
  - It refuses the response with its own result for each of: a wrong
    version, a wrong tag, another command, a wrong size (before the rest is
    read) and a status other than success.
  - It also refuses an MU with no transmit or receive register and a
    request longer than eight words.
  - `tfw_ele_get_info_parse` reads the identity, lifecycle, firmware hash
    and state bytes out of the buffer.
- **What it does not settle.**
  - Which MU, if any, the M7 has to the enclave (question 3), so the
    channel is the caller's data, as with SCMI.
  - The buffer's address as the enclave sees it, and the M7's cache over
    it.
- `test/unit/test_imx95_ele_mu.c` runs it against the register model's ELE
  MU with an enclave written in the test, every word a literal.

### 9.8 The enclave's session

The second piece of section 7's slice 5: a session opened and closed over
section 9.7's message layer, the step before the key store.

- **What RM00284 holds.** §3.1 is `hsm_open_session` and
  `hsm_close_session`, the library calls over these two messages, with at
  most 8 sessions (section 4's table). It gives no ids or sizes.
- **Where the values come from** (imx-secure-enclave d7e1180b):
  - SESSION_OPEN is 0x10 and SESSION_CLOSE 0x11
    (`src/common/include/sab_msg_def.h` lines 26 and 27).
  - The structures are `src/common/sab_msg/include/sab_session.h` lines 11
    to 44. The ELE build defines `PSA_COMPLIANT` (`src/plat/ele/ele.mk` line
    39), which picks their layout:
    - the open request is three words: the header; a reserved byte, the MU's
      interrupt index and a reserved half-word; the priority and the
      operating mode, which `sab_session.c` never sets and so sends as zero;
    - the open response is three words: the header, the status and the
      session handle;
    - the close request is two words, the header and the handle, and its
      response two, the header and the status.
  - The header, tags and status word are section 9.7's.
- **What was built.** `tfw_ele_session_open` returns the handle to its
  caller, which keeps it; the HAL holds no session state.
  `tfw_ele_session_close` sends that handle back.
  - Every refusal is `tfw_ele_call`'s, and a refused open leaves the
    caller's handle as it was.
  - A close the enclave answers with failure is `TFW_ELE_E_HANDLE` (9), a
    result of its own beside the eight of section 9.7, with the status word
    for its rating.
- **What it does not settle.**
  - The interrupt index the M7's MU would name, which needs that MU
    (question 3).
  - Whether the M7's domain may open a session at all (questions 3 and 4).
  - No key store, key, attestation or tier: those are the next pieces.

### 9.9 The enclave's key store

The third piece of section 7's slice 5: a key store opened and closed in
section 9.8's session, the step before a key the enclave holds.

- **What RM00284 holds.** §3.22 is `hsm_open_key_store_service` and
  `hsm_close_key_store_service`, the library calls over these two messages;
  §3.22.1 says the enclave knows a caller by its domain ID and messaging
  unit, and §3.22.3.5 that a STRICT create completes only once the store is
  in NVM. It gives no ids or sizes.
- **Where the values come from** (imx-secure-enclave d7e1180b):
  - KEY_STORE_OPEN is 0x30 and KEY_STORE_CLOSE 0x31
    (`src/common/include/sab_msg_def.h` lines 42 and 43).
  - The structures are `src/common/sab_msg/include/sab_key_store.h` lines 11
    to 50, in the `PSA_COMPLIANT` layout section 9.8 names:
    - the open request is six words: the header; the session handle; the key
      store id; the nonce (`password` in the structure,
      `authentication_nonce` in `include/common/key_store.h` line 23); a word
      whose third byte is the flags and whose others are reserved; a CRC;
    - the CRC is the XOR of the five words before it: a request longer than
      four words carries one (`sab_msg_def.h` line 146,
      `src/common/sab_msg/sab_process_msg.c` lines 235 to 246,
      `src/plat/ele/ele_utils.c` lines 228 to 250);
    - the open response is three words: the header, the status and the key
      store handle (`src/common/sab_msg/sab_key_store.c` line 57);
    - the close request is two words, the header and that handle, and its
      response two, the header and the status.
  - An unknown handle is rated 0x07 (`include/common/err.h` line 25).
- **The flags chosen.** `include/hsm/internal/hsm_key_store.h` lines 35 to 67
  define LOAD (0x00), CREATE (0x01), SHARED, SET_MAC_LEN, KEY_STORE_STATUS,
  MONOTONIC and STRICT_OPERATION. This client sends CREATE when its caller
  asks to create and LOAD when not, and nothing else:
  - not STRICT, nor MONOTONIC, which needs STRICT: a store reaches NVM
    only through the storage messages the enclave sends its host (0xE0 to
    0xE9, `sab_msg_def.h` lines 148 to 156), which this firmware does not
    serve, and the M7 has no store of its own to give it (question 4). A STRICT create would wait on a write nothing makes. A created store
    lives until reset;
  - not KEY_STORE_STATUS: with it the response's last word is a status, not
    a handle (`sab_key_store.h` lines 33 to 38);
  - not SHARED or SET_MAC_LEN: the first shares a store across domains
    before question 3 has said which domain the M7 is, and the second sets a
    MAC length for a MAC this firmware does not compute.
- **What was built.** `tfw_ele_key_store_open` takes the session handle its
  caller keeps and returns the key store handle to it; the HAL holds no
  state. `tfw_ele_key_store_close` sends that handle back.
  - Session handle 0 is `TFW_ELE_E_SESSION` (10), unsent, as the library
    refuses it (`src/common/hsm_api/hsm_key_store.c` line 54); so is the
    enclave's failure rated 0x07, an open on a session it has closed.
  - Any other failure is `TFW_ELE_E_STATUS` with the status word; a refused
    close is `TFW_ELE_E_HANDLE`, as for the session; every other refusal is
    `tfw_ele_call`'s, and a refused open leaves the caller's handle as it
    was.
- **What it does not settle.**
  - The key store id and nonce the M7 would use, and whether its domain may
    hold a store beside go-xkms's on the A55 (question 3).
  - Where a created store persists across reset, if anywhere (question 4).
  - No key, signature, attestation or tier: the next piece is the key
    management service (KEY_MANAGEMENT_OPEN, 0x40, `sab_msg_def.h` line 47).

### 9.10 The enclave's key management service

The fourth piece of section 7's slice 5: the key management service opened
and closed on section 9.9's key store, the step before a key is generated or
used.

- **What RM00284 holds.** §3.2.6.5 and §3.2.6.6 are
  `hsm_open_key_management_service` and `hsm_close_key_management_service`,
  the library calls over these two messages: a caller opens the service to
  generate, update or delete the store's keys. §3.2.2.6 and §3.2.4.7 give its
  argument a flags bitmap and define no bit. It gives no ids or sizes.
- **Where the values come from** (imx-secure-enclave d7e1180b):
  - KEY_MANAGEMENT_OPEN is 0x40 and KEY_MANAGEMENT_CLOSE 0x41
    (`src/common/include/sab_msg_def.h` lines 47 and 48).
  - The structures are `src/common/sab_msg/include/sab_key_management.h`
    lines 11 to 35:
    - the open request is six words: the header; the key store handle; the
      input and output address extensions; a word whose first byte is the
      flags and whose others are reserved; the CRC word of section 9.9;
    - the open response is three words: the header, the status and the key
      management handle (`src/common/sab_msg/sab_key_management.c` line 48);
    - the close request is two words, the header and that handle, and its
      response two, the header and the status.
- **What is sent as zero.** The library zeroes its message buffer
  (`src/common/sab_msg/sab_process_msg.c` line 174) and
  `sab_key_management.c` lines 26 and 27 set only the handle and the flags.
  The address extensions are therefore always zero, and no flag is defined
  (`include/hsm/internal/hsm_key_management.h` line 20, a bare `uint8_t`) and
  its own tests open with the flags cleared (`test/common/test_asymmetric_crypto.c`
  line 423). This client sends all three as zero.
- **What was built.** `tfw_ele_key_mgmt_open` takes the key store handle its
  caller keeps and returns the key management handle to it; the HAL holds no
  state. `tfw_ele_key_mgmt_close` sends that handle back. The CRC word is now
  one helper shared with the key store open.
  - Key store handle 0 is `TFW_ELE_E_KEY_STORE` (11), unsent, as the library
    refuses it (`src/common/hsm_api/hsm_key_management.c` line 28); so is the
    enclave's failure rated 0x07, an open on a store it has closed. It is
    appended after `TFW_ELE_E_SESSION`, which names a session and so does not
    fit a store.
  - Any other failure is `TFW_ELE_E_STATUS` with the status word; a refused
    close is `TFW_ELE_E_HANDLE`; every other refusal is `tfw_ele_call`'s, and
    a refused open leaves the caller's handle as it was.
- **What it does not settle.**
  - Questions 3 and 4, as for section 9.9.
  - No key, signature, attestation or tier: the next piece is key generation
    (KEY_GENERATE, 0x42, `sab_msg_def.h` line 49).

### 9.11 Generating a NIST P-256 key

The fifth piece of section 7's slice 5: one NIST P-256 key generated inside
section 9.10's key management service, its public half written out. It is
not yet used: no signature, attestation or deletion.

- **What RM00284 holds.** `hsm_generate_key`, in section 3.2's key
  management group, is the library call over this message. The subsection
  number and its text were not checked in this slice: the PDF was not to hand
  in the lane, so every value below is from the library.
- **Where the values come from** (imx-secure-enclave d7e1180b):
  - KEY_GENERATE is 0x42 (`src/common/include/sab_msg_def.h` line 49).
  - The structures are `src/common/sab_msg/include/sab_key_generate.h`
    lines 13 to 52, in the PSA_COMPLIANT layout:
    - the request is twelve words: the header; the key management handle;
      the key id; the public key size and, above it, the key group; the key
      type and, above it, the size in bits; the lifetime; the usage; the
      permitted algorithm; the key lifecycle; a word holding the flags, a
      reserved byte and the private key size; the public key's address; the
      CRC word of section 9.9;
    - the response is four words: the header, the status, the key id, and the
      public and private key sizes the enclave expected.
  - NIST P-256 is type 0x7112 (`include/hsm/internal/hsm_key.h` line 112) at
    256 bits (line 173). Its public key is 64 bytes, x then y
    (`test/common/test_key_generate.c` line 57).
  - Sign-hash and verify-hash usage are bits 12 and 13 (`hsm_key.h` lines 50
    and 54); ECDSA with SHA-256 is 0x06000609
    (`include/hsm/internal/hsm_common_def.h` line 35). The caller chooses
    both. The tests use them.
- **What is sent as zero.** `src/common/sab_msg/sab_key_generate.c` fills
  every field from the caller's arguments and leaves the CRC to the message
  layer. This client sends the key lifecycle as zero, the current one. It
  sends the flags, the reserved byte and the private key size as zero too:
  no STRICT operation and no plaintext export
  (`include/hsm/internal/hsm_key_generate.h` lines 45 to 48).
- **What a persistent key needs.** The lifetimes are VOLATILE 0x0,
  PERSISTENT 0x1 and PERSISTENT_PERMANENT 0xFF (`hsm_key.h` lines 80 to 82).
  - A persistent key lives in the external NVM, and its group is written there
    at the next STRICT operation (`hsm_key.h` lines 304 and 305). The STRICT
    operation is flag 0x80, "completed only when the key is written in the
    NVM" (`hsm_key_generate.h` line 48).
  - The library's own persistent vectors set that flag
    (`test/common/test_vectors/psa/test_vectors_set0_kgen_persistent_ap.tv`).
    Their lifetime comment reads 0x00000101, not the header's 0x1.
  - The write goes through the storage messages 0xE0 to 0xE9, which this
    firmware does not serve (section 9.9).
  - This client therefore generates volatile keys only. A volatile key lives
    until reset, with its store. Any other lifetime is refused unsent; the
    tests refuse both 0x1 and 0x101.
- **What was built.** `tfw_ele_key_generate` takes the key management handle
  its caller keeps, a `struct tfw_ele_key_gen` (id, type, lifetime, usage,
  algorithm, group, buffer size and address), and returns the key id. The
  enclave writes the public key into the caller's buffer, as GET_INFO's
  buffer is written. The HAL holds no state. `TFW_ELE_MSG_WORDS` grows from
  8 to 12.
  - Three result codes are appended after 11:
    - `TFW_ELE_E_KEY_MGMT` (12) for handle 0, refused unsent as the library
      refuses it (`src/common/hsm_api/hsm_key_generate.c` line 36), or the
      enclave's failure rated 0x07;
    - `TFW_ELE_E_KEY_TYPE` (13) for a type other than P-256 or a lifetime
      other than volatile, unsent;
    - `TFW_ELE_E_BUFFER` (14) for a buffer under 64 bytes, unsent.

    No existing code names a service, a key or a buffer.
  - Any other failure is `TFW_ELE_E_STATUS` with the status word. Every other
    refusal is `tfw_ele_call`'s. A refused generate leaves the caller's key id
    as it was.
  - The response's expected sizes are not checked; the buffer is checked
    before the send.
- **What it does not settle.**
  - Questions 3 and 4, as for section 9.9. A persistent key waits on
    question 4 and on a host serving the storage messages.
  - No signature, attestation, deletion or tier. The next piece is the
    signature generation service (SIGN_GENERATE) over this key.

### 9.12 Signing with the P-256 key

The sixth piece of section 7's slice 5: the signature generation service
opened on section 9.9's key store, and one ECDSA signature over a SHA-256
digest with section 9.11's key. It is the first command that uses an enclave
key. No application code calls it yet.

- **What RM00284 holds.** `hsm_open_signature_generation_service`,
  `hsm_close_signature_generation_service` and `hsm_generate_signature`, in
  section 3.2's signature generation group, are the library calls over these
  messages. The subsection numbers and their text were not checked in this
  slice: the PDF was not to hand in the lane, so every value below is from the
  library.
- **Where the values come from** (imx-secure-enclave d7e1180b):
  - SIGNATURE_GENERATION_OPEN is 0x70, CLOSE 0x71 and SIGNATURE_GENERATE 0x72
    (`src/common/include/sab_msg_def.h` lines 75 to 77).
  - The structures are `src/common/sab_msg/include/sab_sign_gen.h` lines 11 to
    76, in the PSA_COMPLIANT layout:
    - the open request is six words: the header; the key store handle; the
      input and output address extensions; the flags and three reserved
      bytes; the CRC word of section 9.9. Its response is three: the header,
      the status and the signature generation handle;
    - the close request is the header and that handle, its response the
      header and the status;
    - the signature request is fourteen words: the header; the signature
      generation handle; the key id; the input's address; the signature
      buffer's address; the input's size; the buffer's size, the flags and a
      reserved byte; the scheme id; the salt length and the key type; the
      private key size and the key security size; the SM2 public key's and
      identity's addresses; their sizes; the CRC word. Its response is three
      words: the header, the status, and the signature size the enclave
      expected.
  - ECDSA with SHA-256 is scheme 0x06000609
    (`include/hsm/internal/hsm_sign_gen.h` line 108), the value section 9.11's
    key permits. The signature is r then s, 64 bytes for P-256; the Ry byte
    follows only with the compressed-point flag (lines 169 to 173), which this
    client does not set.
  - The library refuses a key store handle of 0 at the open and a service
    handle of 0 at the signature, unsent
    (`src/common/hsm_api/hsm_sign_gen.c` lines 31 and 129).
- **Digest, not message.** Flag bit 0 chooses the input: clear for a digest,
  set for the message (`hsm_sign_gen.h` lines 71 and 76). This client sends
  the 32-byte SHA-256 digest, flags zero:
  - section 9.11's key carries the sign-hash usage, not sign-message;
  - every row of this project signs a SHA-256 digest it computed itself;
  - a digest keeps the request one size, whatever the caller signs.
- **What is sent as zero.** `src/common/sab_msg/sab_sign_gen.c` lines 82 to
  154 fill the signature request from the caller's arguments. The PSA_COMPLIANT
  open leaves its flags zero (lines 23 to 30). This client sends as zero the
  salt length (no RSA-PSS) and the key type, private key size and security
  size, which are read only with the plaintext-key flag (`hsm_sign_gen.h` lines
  186 to 197). It sends the SM2 fields as zero too.
- **What was built.** `tfw_ele_sign_gen_open` takes the key store handle its
  caller keeps and returns the service's handle; `tfw_ele_sign_gen_close`
  sends it back. `tfw_ele_sign_generate` takes that handle and a
  `struct tfw_ele_sign`: the key id, the key's type and size as the caller
  generated it, the scheme, the digest's address, and the signature buffer's
  address and size. The enclave writes r||s into the caller's buffer, as
  section 9.11's public key is written. The HAL holds no state.
  `TFW_ELE_MSG_WORDS` grows from 12 to 14.
  - A key id alone does not say the key's type. The caller passes the type and
    size it generated the key with. A type other than NIST, a size other than
    256 or a scheme other than ECDSA with SHA-256 is `TFW_ELE_E_KEY_TYPE` (13),
    unsent. The code now reads "a key or scheme this client does not use".
  - A buffer under 64 bytes is `TFW_ELE_E_BUFFER` (14), unsent.
  - One result code is appended after 14: `TFW_ELE_E_SIGN_GEN` (15), for
    service handle 0, unsent, or the enclave's failure rated 0x07. No existing
    code names this service.
  - At the open, store handle 0 or a failure rated 0x07 is
    `TFW_ELE_E_KEY_STORE` (11), as for section 9.10.
  - Any other failure is `TFW_ELE_E_STATUS` with the status word. A refused
    close is `TFW_ELE_E_HANDLE`. Every other refusal is `tfw_ele_call`'s. A
    refused open leaves the caller's handle as it was.
  - The response's expected size is not checked; the buffer is checked before
    the send.
- **What it does not settle.**
  - Questions 3 and 4, as for section 9.9.
  - No signature verification, attestation, deletion or tier. The next piece
    is the public key's export or the enclave's attestation over this key,
    once question 3 says which the application calls.

### 9.13 Exporting the public key and verifying a signature

The seventh piece of section 7's slice 5: section 9.11's public key exported
from the key store by its id, and the signature verification service opened
on the session, verifying one ECDSA P-256 signature over a SHA-256 digest
against that key. With section 9.12 it closes the loop: a key generated,
used to sign, exported and checked. No application code calls it yet.

- **What RM00284 holds.** `hsm_pub_key_recovery`, in the public key recovery
  group, and `hsm_open_signature_verification_service`,
  `hsm_close_signature_verification_service` and `hsm_verify_signature`, in
  section 3.2's signature verification group, are the library calls over
  these messages. The subsection numbers and their text were not checked in
  this slice: the PDF was not to hand in the lane, so every value below is
  from the library.
- **Where the values come from** (imx-secure-enclave d7e1180b):
  - PUB_KEY_RECOVERY is 0x32 (`src/common/include/sab_msg_def.h` line 44).
    "Public Key Recovery is now also known as Public Key Exportation"
    (`include/hsm/internal/hsm_key_recovery.h` lines 16 to 18); this
    client calls it export.
  - Its structures are `src/common/sab_msg/include/sab_key_recovery.h` lines
    13 to 36, in the PSA_COMPLIANT layout. The request is seven words: the
    header; the key store handle; the key id; the output address extension;
    the output address; the output size and two reserved bytes; the CRC word
    of section 9.9. The response is three words: the header, the status, and
    the size written. The key is X||Y, 64 bytes, as the library's own test
    reads it (`test/common/test_key_recovery.c` line 14). The library refuses
    store handle 0, unsent (`src/common/hsm_api/hsm_key_recovery.c` line 31).
  - SIGNATURE_VERIFICATION_OPEN is 0x80, CLOSE 0x81 and SIGNATURE_VERIFY 0x82
    (`sab_msg_def.h` lines 81 to 83). The structures are
    `src/common/sab_msg/include/sab_verify_sign.h` lines 11 to 74:
    - the open request is six words: the header; the session handle; the
      input and output address extensions; the flags and three reserved
      bytes; the CRC word. Its response is three: the header, the status and
      the verification handle;
    - the close request is the header and that handle, its response the
      header and the status;
    - the verify request is fourteen words: the header; the verification
      handle; the key's, the input's and the signature's addresses; the
      input's size; the signature's and the key's sizes; the key security
      size and the key type; the flags and three reserved bytes; the scheme;
      the salt length; the SM2 identity's address; its size; the CRC word.
      Its response is three words: the header, the status, and the
      verification status.
  - The open is on the session, not the key store: the library passes a
    session handle (`src/common/hsm_api/hsm_verify_sign.c` lines 37 to 48),
    and refuses handle 0 at the open and at the verify, unsent (lines 32 and
    134).
  - The verification status is 0x5A3CC3A5 for a signature that holds and
    0x2B4DD4B2 for one that does not (`include/hsm/internal/hsm_verify_sign.h`
    lines 204 and 209). The library reads any word other than the first as
    invalid (`hsm_verify_sign.c` lines 173 to 177).
  - The key travels as plaintext: flags zero is a digest input and a plaintext
    key (`hsm_verify_sign.h` lines 165 and 191; the opaque-key flag is line
    197). Its type is the public one, HSM_PUBKEY_TYPE_ECC_NIST 0x4112
    (`include/hsm/internal/hsm_key.h` line 91), not the key pair's 0x7112
    (line 112), at 256 bits (line 173). The library's own test verifies so,
    with a 64-byte key (`test/common/sign_verify_test.c` lines 108 to 122).
- **What is sent as zero.** `src/common/sab_msg/sab_verify_sign.c` lines 86
  to 156 fill the verify request; the open fills only the session handle
  (lines 16 to 40). This client sends as zero the address extensions, the
  open's flags, the salt length (no RSA-PSS) and the SM2 fields. The export
  sends its address extension as zero (`sab_key_recovery.c` lines 14 to 51).
- **What was built.** `tfw_ele_pub_key_export` takes the key store handle and
  a `struct tfw_ele_key_export`: the key id, the type and size the caller
  generated it with, and the caller's buffer's address and size. The enclave
  writes X||Y there. `tfw_ele_sign_ver_open` takes the session handle and
  returns the service's; `tfw_ele_sign_ver_close` sends it back.
  `tfw_ele_sign_verify` takes that handle and a `struct tfw_ele_verify`: the
  key's type and size, the scheme, and the key's, the digest's and the
  signature's addresses, with the key's and signature's sizes. The HAL holds
  no state and owns no buffer. `TFW_ELE_MSG_WORDS` stays 14.
  - The export carries no key type, so the caller says it. A type other than
    NIST or a size other than 256 is `TFW_ELE_E_KEY_TYPE` (13), unsent; so is
    a verify with another scheme. A buffer, key or signature under 64 bytes
    is `TFW_ELE_E_BUFFER` (14), unsent.
  - Two result codes are appended after 15: `TFW_ELE_E_SIGN_VER` (16), for
    verification handle 0, unsent, or the enclave's failure rated 0x07; and
    `TFW_ELE_E_SIG_BAD` (17), for a verify the enclave carried out whose
    verdict is not 0x5A3CC3A5. A signature that does not hold is thus told
    apart from a transport or status error; the verdict word is returned
    either way.
  - At the export, store handle 0 or a failure rated 0x07 is
    `TFW_ELE_E_KEY_STORE` (11). At the verification open, session 0 or a
    failure rated 0x07 is `TFW_ELE_E_SESSION`.
  - Any other failure is `TFW_ELE_E_STATUS` with the status word. A refused
    close is `TFW_ELE_E_HANDLE`. Every other refusal is `tfw_ele_call`'s. A
    refused open leaves the caller's handle as it was.
  - The export response's size is not checked; the buffer is checked before
    the send.
- **What it does not settle.**
  - Questions 3 and 4, as for section 9.9.
  - No opaque-key verification, attestation, deletion or tier. Whether the
    application verifies on the enclave or in software is question 3's.

## 10. Requirements

One row per requirement of the `imx95` row. "Built" means `firmware-matrix`
links it in CI on Arm's toolchain; nothing runs on the board yet.

| Requirement | State | File | Test |
|---|---|---|---|
| The board as data, id `0x41` (provisional, question 20), in the Cortex-M range | done | `boards/frdm-imx95-m7/` | `test/unit/test_board_frdm-imx95-m7.c` |
| The TCM map at the M7's addresses, the 512-byte vector alignment | built | `mk/arch/cortex-m/imx95.ld`, `boards/frdm-imx95-m7/profile.mk` | `firmware-matrix` (CI); `imx95.ld`'s asserts |
| The LPUART divisor search and the RGPIO bank table | done | `src/hal/cortex-m/imx95/soc_calc.c` | `test/unit/test_imx95_soc_calc.c` |
| LPUART3, RGPIO and WDOG5 behind `hal.h` | met (host, register model) | `src/hal/cortex-m/imx95/hal_uart.c`, `hal_gpio.c`, `hal_soc.c`; the model `test/model/imx95_model.c` | `test/unit/test_imx95_hal.c`: `test_uart_init_*`, `test_tx_*`, `test_rx_*`, `test_the_interrupt_*`, `test_gpio_*`, `test_early_disarms_the_watchdog`, `test_arm_marks_and_sets_eight_seconds`, `test_pet_is_one_refresh_key`, `test_a_locked_watchdog_does_not_park_the_core` |
| Pulse timing, PULSE_IN and PULSE_CAPTURE | built, not yet run on the board | `src/hal/cortex-m/hal_pulse.c`, the one Cortex-M pulse timer, on `TFW_FEATURE_PULSE 1` in `boards/frdm-imx95-m7/board_profile.h`: the pin's level as `tfw_hal_gpio_read` reads it (RGPIO PDIR), the time on SysTick at the processor clock, 800 MHz. SysTick reloads every 1 ms (`TFW_CM_TICK_RELOAD`; the 24-bit counter alone would wrap every 21 ms), and `hal_pulse.c` counts across the reloads | `test/unit/test_cm_pulse.c` (the timer on its model, `test/model/cortex-m/cm_pulse_model.h`) |
| The remoteproc resource table | met | `src/hal/cortex-m/imx95/rsc_table.c`, `mk/arch/cortex-m/image.ld` | `test/unit/test_imx95_rsc_table.c`; as Linux's loader parses it: `test_linux_takes_the_images_table`, `test_the_parser_reads_a_carveout_and_a_vdev`, the mutation `test_a_corrupted_field_is_refused` |
| The boot-HAL pieces specific to the board | none to test | `boot/hal/cortex-m/` has no `imx95/`; `hal_soc.c` joins a tfw-boot link only with a `mk/boot.mk` row, which waits on question 19 | `test/unit/test_imx95_hal.c` covers `hal_soc.c` as the application links it |
| The board in the board sweeps: its part, its registry row, its Go name | done | `scripts/board-check.py` (`mimx9596-m7`), `../boards/board-id-registry.md`, `sdk/go/boardid.go` | `scripts/test_board_check.py`, `sdk/go/boardid_test.go`, `make board-sweep` |
| The boot: tfw-boot on the M7, or AHAB alone | open, the owner's (question 19) | — | — |
| The SCMI client (slice 2 of section 7) | met (host, register model); not called until question 21 is answered | `src/hal/cortex-m/imx95/scmi.c`, `scmi_mu.c`; the model `test/model/imx95_model.c` | `test/unit/test_imx95_scmi.c` (each command's bytes, every status, `test_a_wrong_token_is_refused`, `test_a_wrong_protocol_is_refused`); `test/unit/test_imx95_scmi_mu.c` (`test_a_channel_left_busy_after_the_send_times_out`, `test_a_reply_longer_than_the_channel_is_refused`) |
| Tier value 5 (slice 4) | not started | `include/tfw/types.h`, `testdata/vectors.h`, the Go mirror | the literal vector |
| The enclave's message layer and GET_INFO (the first piece of slice 5, section 9.7) | met (host, register model); not called until question 3 is answered | `src/hal/cortex-m/imx95/ele_mu.c`; the model `test/model/imx95_model.c` | `test/unit/test_imx95_ele_mu.c` (`test_get_info_is_four_words_through_tr0_to_tr3`, `test_a_wrong_tag_is_refused`, `test_a_wrong_version_is_refused`, `test_a_wrong_size_is_refused_unread`, `test_a_failure_status_is_refused_with_its_word`, `test_a_response_that_never_comes_times_out`, `test_the_information_buffer_parses`) |
| The enclave session, open and close (the second piece of slice 5, section 9.8) | met (host, register model); not called until question 3 is answered | `src/hal/cortex-m/imx95/ele_mu.c` | `test/unit/test_imx95_ele_mu.c` (`test_a_good_open_returns_the_handle`, `test_a_good_close_sends_the_handle`, `test_the_handle_the_open_returns_is_the_one_closed`, `test_a_refused_open_leaves_the_handle`, `test_a_close_the_enclave_refuses_is_a_handle_error`, `test_an_open_answered_short_is_refused_unread`, `test_a_close_answered_long_is_refused_unread`, `test_an_open_answered_as_a_close_is_refused`, `test_a_close_answered_as_an_open_is_refused`, `test_an_open_never_answered_times_out`, `test_a_close_never_answered_times_out`) |
| The enclave key store, open and close (the third piece of slice 5, section 9.9) | met (host, register model); not called until questions 3 and 4 are answered | `src/hal/cortex-m/imx95/ele_mu.c` | `test/unit/test_imx95_ele_mu.c` (`test_a_good_key_store_create_returns_the_handle`, `test_a_key_store_load_sends_no_flag`, `test_a_good_key_store_close_sends_the_handle`, `test_the_handles_flow_session_to_key_store_and_back`, `test_a_refused_key_store_open_leaves_the_handle`, `test_a_key_store_open_on_a_closed_session_is_a_session_error`, `test_a_key_store_open_with_no_session_is_not_sent`, `test_a_key_store_close_the_enclave_refuses_is_a_handle_error`, `test_a_key_store_open_answered_short_is_refused_unread`, `test_a_key_store_close_answered_long_is_refused_unread`, `test_a_key_store_open_answered_as_a_close_is_refused`, `test_a_key_store_close_answered_as_a_session_close_is_refused`, `test_a_key_store_open_never_answered_times_out`, `test_a_key_store_close_never_answered_times_out`) |
| One volatile NIST P-256 key generated (the fifth piece of slice 5, section 9.11) | met (host, register model); not called until questions 3 and 4 are answered | `src/hal/cortex-m/imx95/ele_mu.c` | `test/unit/test_imx95_ele_mu.c` (`test_a_good_key_generate_returns_the_id_and_the_public_key`, `test_a_refused_key_generate_leaves_the_id`, `test_a_key_generate_on_a_closed_service_is_a_key_management_error`, `test_a_key_generate_with_no_service_is_not_sent`, `test_a_key_generate_into_a_buffer_too_small_is_not_sent`, `test_a_key_type_other_than_p256_is_not_sent`, `test_a_persistent_key_is_not_sent`, `test_a_key_generate_answered_short_is_refused_unread`, `test_a_key_generate_answered_as_a_key_management_open_is_refused`, `test_a_key_generate_never_answered_times_out`, `test_the_handles_flow_session_to_a_generated_key_and_back`) |
| The signature generation service and one ECDSA P-256 signature over a SHA-256 digest (the sixth piece of slice 5, section 9.12) | met (host, register model); not called until questions 3 and 4 are answered | `src/hal/cortex-m/imx95/ele_mu.c` | `test/unit/test_imx95_ele_mu.c` (`test_a_good_signature_open_returns_the_handle`, `test_a_signature_open_on_a_closed_store_is_a_key_store_error`, `test_a_good_signature_close_sends_the_handle`, `test_a_signature_close_on_a_closed_handle_is_a_handle_error`, `test_a_good_sign_returns_r_and_s_in_the_callers_buffer`, `test_a_refused_sign_is_a_status_error`, `test_a_sign_on_a_closed_service_is_a_signature_service_error`, `test_a_sign_with_no_service_is_not_sent`, `test_a_sign_with_a_key_type_other_than_nist_is_not_sent`, `test_a_sign_with_a_key_other_than_p256_is_not_sent`, `test_a_sign_into_a_buffer_too_small_is_not_sent`, `test_a_sign_with_another_scheme_is_not_sent`, `test_a_sign_answered_short_is_refused_unread`, `test_a_sign_answered_as_a_key_generate_is_refused`, `test_a_sign_never_answered_times_out`, `test_the_handles_flow_session_to_a_signature_and_back`) |
| The public key's export and the signature verification service, one ECDSA P-256 verify over a SHA-256 digest (the seventh piece of slice 5, section 9.13) | met (host, register model); not called until questions 3 and 4 are answered | `src/hal/cortex-m/imx95/ele_mu.c` | `test/unit/test_imx95_ele_mu.c` (`test_a_good_export_returns_x_and_y_in_the_callers_buffer`, `test_a_refused_export_is_a_status_error`, `test_an_export_on_a_closed_store_is_a_key_store_error`, `test_an_export_with_no_store_is_not_sent`, `test_an_export_of_a_key_type_other_than_nist_is_not_sent`, `test_an_export_of_a_key_other_than_p256_is_not_sent`, `test_an_export_into_a_buffer_too_small_is_not_sent`, `test_an_export_answered_short_is_refused_unread`, `test_an_export_answered_as_a_key_generate_is_refused`, `test_an_export_never_answered_times_out`, `test_a_good_verification_open_returns_the_handle`, `test_a_verification_open_on_a_closed_session_is_a_session_error`, `test_a_verification_open_with_no_session_is_not_sent`, `test_a_good_verification_close_sends_the_handle`, `test_a_verification_close_on_a_closed_handle_is_a_handle_error`, `test_a_good_verify_passes`, `test_a_verify_that_fails_is_a_bad_signature_not_a_transport_error`, `test_a_verify_status_neither_passed_nor_failed_is_a_bad_signature`, `test_a_refused_verify_is_a_status_error`, `test_a_verify_on_a_closed_service_is_a_verification_service_error`, `test_a_verify_with_no_service_is_not_sent`, `test_a_verify_with_another_scheme_is_not_sent`, `test_a_verify_with_a_key_other_than_p256_is_not_sent`, `test_a_verify_with_a_short_key_is_not_sent`, `test_a_verify_with_a_short_signature_is_not_sent`, `test_a_verify_answered_short_is_refused_unread`, `test_a_verify_answered_as_a_sign_is_refused`, `test_a_verify_never_answered_times_out`, `test_a_generated_key_signs_exports_and_verifies`) |
| The enclave provider (slice 5): session, key store, signing | not started | `src/crypto/ele/` | host unit against a messaging-unit model (questions 3, 4, 6, 16) |
| Our OTA gate on the M7 (slice 6) | not started | the receive path | host unit (questions 8 to 12, 19) |
| Ethernet (slice 7) | not started | behind `include/tfw/eth.h` | question 14 |
| The bench (slice 8) | not started | the board page | end to end on the bench host |

**What question 19 changes.** Neither answer touches the board's data, the
SoC directory, the resource table or the sweeps. *AHAB alone*: the 32 KiB
reserved for tfw-boot at the ITCM's base, `BOOT_*` in `profile.mk`, the
`TFW_BOOT_REGION_*` defines and `board-check`'s `boot_end` for `mimx9596-m7`
go, the application moves to the ITCM's base, the handoff block leaves the
DTCM, and the OTA gate is slice 6's install-time check. *tfw-boot on the M7*:
the board gets a `mk/boot.mk` row in the reserved region, as `mps2-an505` has,
and tfw-boot's trailer, SUIT and soft-root code runs on it unchanged. If
remoteproc is ruled out as a loader, the resource table and its `.ld` lines go.
