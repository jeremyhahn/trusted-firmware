# Reference: the standards this repository implements

> **Status: REFERENCE** · Scope: `docs/reference/` · Last verified: 2026-10-20
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
| Infineon, *OPTIGA™ Trust M Solution Reference Manual* (SLS 32AIA010MH/S/K/L/M) | Revision 3.60, 2023-12-04 (SHA-256 `87400b7f29003d66e297ff9762dd262e1d663d2bce3c3c3d814dcb99ca68f3b2`) | <https://raw.githubusercontent.com/Infineon/optiga-trust-m/develop/documents/OPTIGA_Trust_M_Solution_Reference_Manual_v3.60.pdf> | The read-only commands only: OpenApplication (§4.4.1.1, Table 40), CloseApplication (§4.4.1.2, Table 41), GetDataObject (§4.4.1.3, Table 42), GetRandom (§4.4.1.6, Table 45), the APDU of Table 37 and the error codes of Table 39: `src/crypto/optiga/optiga_cmd.c`. No write command (SetDataObject, SetObjectProtected, key generation, counters, lifecycle) is implemented |

The TCG specifications' licence does not allow a copy here; Infineon's
document carries a copyright and no licence to redistribute.

## Board sources

Not standards: the vendor documents a board profile is checked against. Listed
by title and URL, because none publishes a licence that allows a copy.

| Document | Retrieved | URL | Checked against it |
|---|---|---|---|
| Waveshare, *ESP32-P4-WIFI6-POE-ETH Schematic* (3 pages; SHA-256 `885b8f038881c6656486fea143bcf749f17c6a1ec9006cc693dcdb298018380b`) | 2026-10-18 | <https://files.waveshare.com/wiki/ESP32-P4-WIFI6-POE-ETH/ESP32-P4-WIFI6-POE-ETH-Schematic.pdf> | Every pin and the flash part of `boards/esp32p4-poe/`: [`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md), "Pins" |
| Waveshare, *ESP32-P4-NANO Schematic* | 2026-10-18 | <https://files.waveshare.com/wiki/ESP32-P4-NANO/ESP32-P4-NANO-schematic.pdf> | Ruled out as the bench board: its flash is 16 MB |

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
