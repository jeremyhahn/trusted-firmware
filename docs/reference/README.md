# Reference: the standards this repository implements

> **Status: REFERENCE** · Scope: `docs/reference/` · Last verified: 2026-10-21
> One entry per standard the code implements, with the copy it is implemented against.

Handbook `software/conventions/documentation.md` §9: where a standard exists,
this project implements it precisely and designs nothing in its place. A text
whose licence allows redistribution is copied here (IETF RFCs and drafts in text
form from rfc-editor.org and ietf.org, NIST FIPS in the public domain); one that
does not is listed with its official URL and is not copied. A draft is
implemented at one pinned revision, watched from [`../../ROADMAP.md`](../../ROADMAP.md)
Dependencies.

Only what our code implements is listed, and where it implements part of a
standard the entry says which part. Standards consumed through go-xkms (DICE
certificate profiles and attestation verification, ACME device-attest-01, RATS
RFC 9334) and through hardware (ECDSA P-256 in the ATECC608) are not ours.

## Copied

| File | Standard | Revision | Implemented by |
|---|---|---|---|
| [`NIST.FIPS.180-4.pdf`](NIST.FIPS.180-4.pdf) | Secure Hash Standard (SHA-256) | FIPS 180-4, August 2015 | `src/crypto/sha256.c` |
| [`rfc2104.txt`](rfc2104.txt) | HMAC: Keyed-Hashing for Message Authentication | RFC 2104 | `src/crypto/sha256.c` (HMAC-SHA256); `boot/tier/cdi_soft.c` |
| [`rfc8032.txt`](rfc8032.txt) | Edwards-Curve Digital Signature Algorithm (EdDSA) | RFC 8032 | Ed25519 verify only: `boot/verify/verify_ed25519_donna.c` over the vendored `src/third_party/ed25519-donna/` |
| [`rfc8610.txt`](rfc8610.txt) | Concise Data Definition Language (CDDL): A Notational Convention to Express CBOR and JSON Data Structures | RFC 8610 | No code yet: the notation of the policy-set and script payloads, [`../hsm-self-defence/adr-response-policy-format.md`](../hsm-self-defence/adr-response-policy-format.md) D5, which the row's payload reader will implement |
| [`rfc8949.txt`](rfc8949.txt) | Concise Binary Object Representation (CBOR) | RFC 8949 | `src/caps/uccs.c` (deterministic encoding, §4.2.1); `src/core/suit.c` (reader); `tools/suit-emit/` |
| [`rfc9711.txt`](rfc9711.txt) | The Entity Attestation Token (EAT) | RFC 9711 | Claims set only (`eat_nonce`, `ueid`, `measurements`): `src/caps/uccs.c`; `ueid` (§4.2.1, type `0x01`) in `tfwprov --record`'s provisioning record: `sdk/go/cmd/tfwprov/record.go` |
| [`rfc7517.txt`](rfc7517.txt) | JSON Web Key (JWK) | RFC 7517 | The DeviceID public key in `tfwprov --record`'s provisioning record: `sdk/go/cmd/tfwprov/record.go` |
| [`rfc7518.txt`](rfc7518.txt) | JSON Web Algorithms (JWA) | RFC 7518 | EC P-256 key members `kty`, `crv`, `x`, `y` (§6.2.1) of that JWK: `sdk/go/cmd/tfwprov/record.go` |
| [`rfc7800.txt`](rfc7800.txt) | Proof-of-Possession Key Semantics for JSON Web Tokens (JWTs) | RFC 7800 | The `cnf` claim's `jwk` member (§3.2) in that record: `sdk/go/cmd/tfwprov/record.go` |
| [`rfc9393.txt`](rfc9393.txt) | Concise Software Identification Tags (CoSWID) | RFC 9393 | The minimal evidence tag in `measurements`: `src/caps/uccs.c` |
| [`rfc9052.txt`](rfc9052.txt) | CBOR Object Signing and Encryption (COSE): Structures and Process | RFC 9052 | `COSE_Sign1_Tagged` and `Sig_structure`, detached payload: `src/core/suit.c`, `tools/suit-emit/` (signing is go-xkms's) |
| [`rfc9053.txt`](rfc9053.txt) | COSE: Initial Algorithms | RFC 9053 | ES256 (`-7`) identifier and raw r‖s form: `src/core/suit.c` |
| [`rfc9054.txt`](rfc9054.txt) | COSE: Hash Algorithms | RFC 9054 | SHA-256 (`-16`) identifier: `src/core/suit.c`, `tools/suit-emit/` |
| [`rfc5280.txt`](rfc5280.txt) | Internet X.509 PKI Certificate and CRL Profile | RFC 5280 | DER `TBSCertificate` encoding (§4.1) of the alias certificate the device mints: `src/caps/alias.c` (the certificate profile is go-xkms's) |
| [`rfc5758.txt`](rfc5758.txt) | Internet X.509 PKI: Additional Algorithms and Identifiers for DSA and ECDSA | RFC 5758 | `ecdsa-with-SHA256` AlgorithmIdentifier (§3.2): `src/caps/alias.c`, `src/caps/csr.c` |
| [`rfc2986.txt`](rfc2986.txt) | PKCS #10: Certification Request Syntax | RFC 2986 | `CertificationRequestInfo` (§4.1) of the possession CSR: `src/caps/csr.c` |
| [`rfc4043.txt`](rfc4043.txt) | Internet X.509 PKI Permanent Identifier | RFC 4043 | `id-on-permanentIdentifier` subjectAltName: `src/caps/csr.c` |
| [`rfc2131.txt`](rfc2131.txt) | Dynamic Host Configuration Protocol | RFC 2131 | The client's INIT, SELECTING, REQUESTING, BOUND, RENEWING and REBINDING, the ARP probe and DHCPDECLINE (no DHCPRELEASE; T1 and T2 always the §4.4.5 defaults): `src/net/dhcp.c` |
| [`rfc2132.txt`](rfc2132.txt) | DHCP Options and BOOTP Vendor Extensions | RFC 2132 | Options 1, 3, 50, 51, 53, 54, 55 and the pad and end options; 52 is recognised only to discard the message: `src/net/dhcp.c` |
| [`draft-ietf-suit-manifest-37.txt`](draft-ietf-suit-manifest-37.txt) | A Concise Binary Object Representation (CBOR)-based Serialization Format for the Software Updates for Internet of Things (SUIT) Manifest | draft, **pinned -37**; this copy omits the one example private key in Appendix B, which the repository's secret scan rejects | The subset in `docs/ota/adr-ota-image-format-and-trust.md`: `src/core/suit.c`, `tools/suit-emit/` |
| [`draft-ietf-suit-mti-23.txt`](draft-ietf-suit-mti-23.txt) | Mandatory-to-Implement Algorithms for Authors and Recipients of SUIT Manifests | draft, **pinned -23** | Authentication half of `suit-sha256-es256-ecdh-a128ctr`: `src/core/suit.c` |

**How the drafts were pinned.** No ADR or code comment names a revision. The
reader and the ADR were written on 2026-10-15, when -37 (2026-06-18) and -23
(2025-07-22) were the current revisions on datatracker. The reader's keys (envelope
tag 107, authentication wrapper 2, sequence number 2, shared sequence 4,
override-parameters 20, vendor id 1, image size 14) were checked against -37's CDDL.

## Not redistributable

| Standard | Revision | Official URL | Implemented by |
|---|---|---|---|
| TCG Hardware Requirements for a Device Identifier Composition Engine | Revision 78 | <https://trustedcomputinggroup.org/resource/hardware-requirements-for-a-device-identifier-composition-engine/> | Layer 0 CDI = HMAC(UDS, H(code)), form (2) of §5: `boot/boot_cdi.h`, `boot/tier/cdi_soft.c`, `boot/tier/cdi_atecc.c` |
| TCG DICE Attestation Architecture | Revision 23 | <https://trustedcomputinggroup.org/resource/dice-attestation-architecture/> | `tcg-dice-UCCS-evidence` (2.23.133.5.4.6, §6.1.4) in the alias certificate: `src/caps/alias.c`, `src/caps/uccs.c` |
| TCG DICE Symmetric Identity Based Device Attestation | revision not cited in code | <https://trustedcomputinggroup.org/resource/dice-symmetric-identity-based-device-attestation/> | The HMAC-SHA256 identity (`TFW_ALG_HMAC_SHA256`): `include/tfw/protocol.h`, `xkms/avr/algorithms.go` |
| Infineon, *IFX I2C Protocol, Protocol Specification* | Revision 2.03, 2020-09-22 (SHA-256 `04288206e14412cd6dc6bbbdbc45fc469a28a1cc260b2e42e022d8f33397774d`) | <https://raw.githubusercontent.com/Infineon/optiga-trust-m/develop/documents/Infineon_I2C_Protocol_v2.03.pdf> | Layers 1 to 4 (§2 to §5), one master, window size 1; not the presentation layer (§6): `src/crypto/optiga/optiga_i2c.c`. The FCS goes high byte first, as Infineon's host library sends it, not as §3.3's text reads |
| Infineon, *OPTIGA™ Trust M Solution Reference Manual* (SLS 32AIA010MH/S/K/L/M) | Revision 3.70, 2024-10-09 (SHA-256 `41315a418c287f9cfff54b51a3538eec8026de2a601d83a6196b69aeee9adc4d`), the newest Infineon publishes; its revision history gives "Editorial changes" only, and every section and table cited here reads as in 3.60 | <https://raw.githubusercontent.com/Infineon/optiga-trust-m-overview/main/docs/pdf/OPTIGA_Trust_M_Solution_Reference_Manual_v3.70.pdf> | The read-only commands only: OpenApplication (§4.4.1.1, Table 40), CloseApplication (§4.4.1.2, Table 41), GetDataObject (§4.4.1.3, Table 42), GetRandom (§4.4.1.6, Table 45), CalcSign with the factory key `0xE0F0` (§4.4.1.12, Table 51, Table 69), VerifySign with a P-256 key in the command (§4.4.1.13, Table 52, Table 57, §6.2.3), the APDU of Table 37 and the error codes of Table 39: `src/crypto/optiga/optiga_cmd.c`. The provider, `src/crypto/optiga/optiga_crypto.c`, also reads the monotonic counters `0xE120`-`0xE123` with GetDataObject (Tables 68 and 73). No write command (SetDataObject, SetObjectProtected, key generation, counters, lifecycle) is implemented |
| The Linux kernel, *Remote Processor Framework* (`Documentation/staging/remoteproc.rst`) | mainline, retrieved 2026-10-20 (SHA-256 `feecd9edc442f75a9fe27c4ccd54a1cb35b56219e838410303d8401622f116d9`) | <https://docs.kernel.org/staging/remoteproc.html> | "Binary Firmware Structure": the resource table header (`struct resource_table`) with no entry, in the section the kernel's ELF loader looks up by name, `.resource_table` (`drivers/remoteproc/remoteproc_elf_loader.c`, `find_table()`: version 1 only, reserved words zero). Implemented in `src/hal/cortex-m/imx95/rsc_table.c` and placed by `mk/arch/cortex-m/mps2-tz.ld`; `mk/arch/cortex-m/imx95.ld` checks it |
| Arm, *System Control and Management Interface* (SCMI), DEN0056 | version 3.2, issue E (DEN0056E, 210 pages), read 2026-10-21 | <https://developer.arm.com/documentation/den0056/e> | The message header (§4.1.2, Tables 2 and 3), protocol discovery (§4.1.3, Table 4) and status codes (§4.1.4, Table 5); the Base protocol's PROTOCOL_VERSION and PROTOCOL_ATTRIBUTES, the Clock protocol's CLOCK_CONFIG_SET and CLOCK_RATE_GET, the Power domain management protocol's POWER_STATE_SET and the Reset domain management protocol's RESET: `src/hal/cortex-m/imx95/scmi.c`. The shared memory transport (§5.1, §5.1.1) and its area layout (§5.1.2, Tables 25 and 26): `src/hal/cortex-m/imx95/scmi_mu.c`. The text is not held here; the code cites section and table, each checked against DEN0056E. The one name corrected is CLOCK_CONFIG_SET's third word, `extended_config_val` (§4.6.2.9), which the code had called `oem_config_val` |
| NXP, *EdgeLock Enclave Hardware Security Module API*, RM00284 | Rev. 4.5, read 2026-10-21 | <https://www.nxp.com/docs/en/reference-manual/RM00284.pdf> | The library API only: it defines no message header, status word, command id or MU register, so `src/hal/cortex-m/imx95/ele_mu.c` takes none from it. §1 names the reference implementation below; §3.13.2.2 the lifecycle field GET_INFO returns; §3.13.3.1 (`hsm_dev_getinfo`) and §3.19.3.1 (`hsm_get_info`) are the library calls over the same message, each behind a library session the wire message does not carry; §3.1 (`hsm_open_session`, `hsm_close_session`) the library calls over SESSION_OPEN and SESSION_CLOSE; §3.22 (`hsm_open_key_store_service`, `hsm_close_key_store_service`) the library calls over KEY_STORE_OPEN and KEY_STORE_CLOSE; §3.2.6.5 and §3.2.6.6 (`hsm_open_key_management_service`, `hsm_close_key_management_service`) the library calls over KEY_MANAGEMENT_OPEN and KEY_MANAGEMENT_CLOSE, with §3.2.2.6 and §3.2.4.7 their undefined flags bitmap; `hsm_generate_key`, in §3.2's key management group, the library call over KEY_GENERATE (its subsection not checked against the PDF in the slice that added it; every KEY_GENERATE value is from the library); `hsm_open_signature_generation_service`, `hsm_close_signature_generation_service` and `hsm_generate_signature`, in §3.2's signature generation group, the library calls over SIGNATURE_GENERATION_OPEN, SIGNATURE_GENERATION_CLOSE and SIGNATURE_GENERATE (their subsections not checked against the PDF in the slice that added them; every value is from the library); `hsm_pub_key_recovery`, in the public key recovery group, the library call over PUB_KEY_RECOVERY, and `hsm_open_signature_verification_service`, `hsm_close_signature_verification_service` and `hsm_verify_signature`, in §3.2's signature verification group, the library calls over SIGNATURE_VERIFICATION_OPEN, SIGNATURE_VERIFICATION_CLOSE and SIGNATURE_VERIFY (their subsections not checked against the PDF in the slice that added them; every value is from the library) |
| NXP, imx-secure-enclave (BSD-3-Clause) | commit d7e1180b, read 2026-10-21 | <https://github.com/nxp-imx/imx-secure-enclave> | The message header (`src/common/sab_msg_def.h` struct sab_mu_hdr, MESSAGING_VERSION_6), the status word (GET_STATUS_CODE, GET_RATING_CODE, SAB_FAILURE_STATUS) and GET_INFO (ROM_DEV_GETINFO_REQ; `src/common/sab_dev_getinfo.h` request, response and the dev_info/dev_addn_info buffer); the session (SAB_SESSION_OPEN_REQ and SAB_SESSION_CLOSE_REQ in `src/common/include/sab_msg_def.h`, the structures in `src/common/sab_msg/include/sab_session.h`, their PSA_COMPLIANT layout per `src/plat/ele/ele.mk`); the key store (SAB_KEY_STORE_OPEN_REQ and SAB_KEY_STORE_CLOSE_REQ, the structures in `src/common/sab_msg/include/sab_key_store.h`, the flags in `include/hsm/internal/hsm_key_store.h`, the CRC word in `src/plat/ele/ele_utils.c`, the unknown-handle rating in `include/common/err.h`); the key management service (SAB_KEY_MANAGEMENT_OPEN_REQ and SAB_KEY_MANAGEMENT_CLOSE_REQ, the structures in `src/common/sab_msg/include/sab_key_management.h`, the zeroed buffer in `src/common/sab_msg/sab_process_msg.c`); key generation (SAB_KEY_GENERATE_REQ, the structures in `src/common/sab_msg/include/sab_key_generate.h`, the key type, size, usage and lifetimes in `include/hsm/internal/hsm_key.h`, the flags in `include/hsm/internal/hsm_key_generate.h`, ECDSA SHA-256 in `include/hsm/internal/hsm_common_def.h`, the public key size in `test/common/test_key_generate.c`); signature generation (SAB_SIGNATURE_GENERATION_OPEN_REQ, SAB_SIGNATURE_GENERATION_CLOSE_REQ and SAB_SIGNATURE_GENERATE_REQ, the structures in `src/common/sab_msg/include/sab_sign_gen.h`, the fields filled in `src/common/sab_msg/sab_sign_gen.c`, the digest flag, ECDSA SHA-256 scheme and r||s format in `include/hsm/internal/hsm_sign_gen.h`, the handle checks in `src/common/hsm_api/hsm_sign_gen.c`); public key export (SAB_PUB_KEY_RECOVERY_REQ, the structures in `src/common/sab_msg/include/sab_key_recovery.h`, the fields filled in `src/common/sab_msg/sab_key_recovery.c`, the export name in `include/hsm/internal/hsm_key_recovery.h`, the store handle check in `src/common/hsm_api/hsm_key_recovery.c`, the 64-byte key in `test/common/test_key_recovery.c`); signature verification (SAB_SIGNATURE_VERIFICATION_OPEN_REQ, SAB_SIGNATURE_VERIFICATION_CLOSE_REQ and SAB_SIGNATURE_VERIFY_REQ, the structures in `src/common/sab_msg/include/sab_verify_sign.h`, the fields filled in `src/common/sab_msg/sab_verify_sign.c`, the digest and plaintext-key flags and the verification status words in `include/hsm/internal/hsm_verify_sign.h`, the public key type in `include/hsm/internal/hsm_key.h`, the session handle and handle checks in `src/common/hsm_api/hsm_verify_sign.c`, the plaintext verify in `test/common/sign_verify_test.c`): `src/hal/cortex-m/imx95/ele_mu.c` |
| NXP, U-Boot (GPL-2.0) | commit ece349ad, read 2026-10-21 | <https://github.com/nxp-imx/uboot-imx> | The command and response tags (0x17, 0xE1), success 0xD6 and the MU word order (`arch/arm/include/asm/mach-imx/ele_api.h`, `drivers/misc/imx_ele/ele_mu.c`, `ele_api.c` ele_get_info), agreeing with imx-secure-enclave on every other value: `src/hal/cortex-m/imx95/ele_mu.c`. Read for facts only; no code is copied |
| Linux, `drivers/mailbox/imx-mailbox.c` (GPL-2.0) | v6.12, read 2026-10-21 | <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/mailbox/imx-mailbox.c?h=v6.12> | A secondary source for the MU registers (GCR, and for `fsl,imx95-mu-ele` PAR, TSR, RSR, TR, RR) until IMX95RM is held: `src/hal/cortex-m/imx95/scmi_mu.h`, `ele_mu.h`. Read for facts only |

Each TCG entry is a TCG specification; its licence does not allow a copy here.
Arm's licence for DEN0056 does not grant redistribution, so it is listed, not
copied.
NXP's licence for RM00284 does not grant redistribution; the three source
trees are listed rather than vendored, and none of their code is copied.
The kernel's documentation is GPL-2.0, which does not fit this repository's
commercial licence, so it is listed, not copied.
Infineon's documents carry a copyright and no licence to redistribute.

## Board sources

Not standards: the vendor documents a board profile is checked against. Listed
by title and URL, because none publishes a licence that allows a copy.

| Document | Retrieved | URL | Checked against it |
|---|---|---|---|
| Waveshare, *ESP32-P4-WIFI6-POE-ETH Schematic* (3 pages; SHA-256 `885b8f038881c6656486fea143bcf749f17c6a1ec9006cc693dcdb298018380b`) | 2026-10-18 | <https://files.waveshare.com/wiki/ESP32-P4-WIFI6-POE-ETH/ESP32-P4-WIFI6-POE-ETH-Schematic.pdf> | Every pin and the flash part of `boards/esp32p4-poe/`: [`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md), "Pins" |
| Waveshare, *ESP32-P4-NANO Schematic* | 2026-10-18 | <https://files.waveshare.com/wiki/ESP32-P4-NANO/ESP32-P4-NANO-schematic.pdf> | Ruled out as the bench board: its flash is 16 MB |
| Atmel, *ATmega640/V-1280/V-1281/V-2560/V-2561/V Datasheet*, Atmel-2549Q-AVR-02/2014 | 2026-10-21 | <https://ww1.microchip.com/downloads/en/devicedoc/atmel-2549-8-bit-avr-microcontroller-atmega640-1280-1281-2560-2561_datasheet.pdf> | `BOOT_LOCK_PROGRAMMED` in `boards/atmega2560/profile.mk`: §30.1 Table 30-1 (lock bit byte), §29.4 Tables 29-2 and 29-3 (boot lock modes); [`../security/fuses-and-lockbits.md`](../security/fuses-and-lockbits.md) |
| Microchip, *ATmega48A/PA/88A/PA/168A/PA/328/P Data Sheet Complete*, DS40002061B (2020) | 2026-10-21 | <https://ww1.microchip.com/downloads/en/DeviceDoc/ATmega48A-PA-88A-PA-168A-PA-328-P-DS-DS40002061B.pdf> | `BOOT_LOCK_PROGRAMMED` in `boards/atmega328p/profile.mk`: §28.1 Table 28-1, §27.5 Tables 27-2 and 27-3; the same page |
| Atmel, *ATmega16U4/32U4 Datasheet*, Atmel-7766J-USB-ATmega16U4/32U4-Datasheet_04/2016 | 2026-10-21 | <https://ww1.microchip.com/downloads/en/devicedoc/atmel-7766-8-bit-avr-atmega16u4-32u4_datasheet.pdf> | `BOOT_LOCK_PROGRAMMED` in `boards/atmega32u4/profile.mk`: §28.1 Table 28-1, §27.4 Tables 27-2 and 27-3; the same page |

## Cortex-M sources

Not standards: the vendor documents the Cortex-M ADRs (#14, #15) are checked
against. Listed by title, revision and URL, not copied: no licence review has
cleared a copy into this repository. A revision is a release tag or a full commit.

| Document | Revision | URL | Checked against it |
|---|---|---|---|
| Trusted Firmware-M, *TF-M builtin keys* | v2.3.1 | <https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/design_docs/tfm_builtin_keys.html> | The HUK as a derivation key, "Multi-partition key derivation": [`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md), "Where the DICE root lives, per part" |
| Nordic, nRF Connect SDK, *KMU and CRACEN overview* (`doc/nrf/app_dev/device_guides/kmu_guides/kmu_cracen_overview.rst`) | sdk-nrf `94c7193d404ccb023e4b2f09a595149de681028e` | <https://github.com/nrfconnect/sdk-nrf/blob/94c7193d404ccb023e4b2f09a595149de681028e/doc/nrf/app_dev/device_guides/kmu_guides/kmu_cracen_overview.rst> | The nRF54L15 seed, the IKG and its identity key, "CRACEN Isolated Key Generator": the same section of #15 |
| Nordic, nRF Connect SDK, *nRF54L memory protection* (`doc/nrf/app_dev/bootloaders_dfu/mcuboot_nsib/bootloader_nrf54l_memory_protection.rst`) | sdk-nrf `94c7193d404ccb023e4b2f09a595149de681028e` | <https://github.com/nrfconnect/sdk-nrf/blob/94c7193d404ccb023e4b2f09a595149de681028e/doc/nrf/app_dev/bootloaders_dfu/mcuboot_nsib/bootloader_nrf54l_memory_protection.rst> | The immutable boot region, "At reset (before bootloader software runs)" and "At NSIB hand-off": the same section of #15 |
| ST, STM32CubeH5, *ROT_Provisioning STiROT* (`Projects/STM32H573I-DK/ROT_Provisioning/STiROT/README.md`) | `bac48badf93cb496f6445afba956082490d3f543` | <https://github.com/STMicroelectronics/STM32CubeH5/blob/bac48badf93cb496f6445afba956082490d3f543/Projects/STM32H573I-DK/ROT_Provisioning/STiROT/README.md> | STiRoT's two services: the same section of #15 |
| ST, STM32CubeH5, *OEMiROT_Boot* (`Projects/STM32H573I-DK/Applications/ROT/OEMiROT_Boot/README.md`) | `bac48badf93cb496f6445afba956082490d3f543` | <https://github.com/STMicroelectronics/STM32CubeH5/blob/bac48badf93cb496f6445afba956082490d3f543/Projects/STM32H573I-DK/Applications/ROT/OEMiROT_Boot/README.md> | OEMiRoT on MCUboot, keys in OBKeys: the same section of #15 |
| QEMU, `hw/arm/mps2-tz.c` | v10.0.0 | <https://gitlab.com/qemu-project/qemu/-/blob/v10.0.0/hw/arm/mps2-tz.c> | `mps2-an505` has no key store or one-time memory: the same section of #15; the CI target, #14 decision 3 |

ST's RM0481, the STM32H563/H573 reference manual, could not be retrieved on
2026-10-20, so every H573 fact that needs it is marked "to confirm" (#15 question
13; [`../multi-arch/adr-stm32h573i-dk-onboarding.md`](../multi-arch/adr-stm32h573i-dk-onboarding.md)).
