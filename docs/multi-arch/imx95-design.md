# i.MX 95 Cortex-M7 board: the design of row `imx95`

> **Status: PROPOSED** · Issue: [#49](../../../../issues/49) · SPEC row: `imx95`
> (future) · Scope: trusted-firmware (docs only) · Last verified: 2026-10-18
>
> #49's first deliverable: the row's design. It changes no code, no build file
> and no SPEC row. SPEC rows change at merge (CA m623).

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
  decision 12 and its rejected alternatives ("OTA ADR d12"); the #50 design on
  `docs/50-hsm-self-defence-design` (`hsm-self-defence-design.md`, unmerged);
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
| `tfw_hal_adc_init`, `_set_ref`, `_read` | — | not linked in the first slices | — |
| `tfw_hal_pwm_write`, `_stop`; `tfw_hal_tone_start`, `_stop` | — | not linked in the first slices (TPM later) | IMX95CEC Figure 3 |
| `tfw_hal_pulse_in`, `tfw_hal_pulse_capture` | — | not linked | — |
| `tfw_hal_eeprom_read`, `_write` | — | not linked; `TFW_EEPROM_SIZE 0` | ref ADR d4 |
| `tfw_hal_nvm_read` | SoC | the platform block; where it lives is open | ref ADR Q4; question 6 |
| `tfw_hal_flash_read` | SoC | a copy from the running image in TCM (the trailer and SUIT reader read the image through it) | section 2.3 |
| `tfw_hal_handoff_block` | — | not linked: no tfw-boot, so no handoff | ref ADR d4 |
| `tfw_hal_i2c_init`, `_xfer`; `tfw_hal_spi_init`, `_xfer` | — | not linked in the first slices (LPI2C, LPSPI later) | ref ADR d4 |
| `tfw_hal_ow_reset`, `_write`, `_read` | — | not linked | — |
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
   `hsm_dev_attest`? IMX95RM (MU chapter), IMX95SRM.
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
