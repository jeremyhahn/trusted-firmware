# Reference: the standards this repository implements

> **Status: REFERENCE** · Scope: `docs/reference/` · Last verified: 2026-10-18
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
| [`rfc8949.txt`](rfc8949.txt) | Concise Binary Object Representation (CBOR) | RFC 8949 | `src/caps/uccs.c` (deterministic encoding, §4.2.1); `src/core/suit.c` (reader); `tools/suit-emit/` |
| [`rfc9711.txt`](rfc9711.txt) | The Entity Attestation Token (EAT) | RFC 9711 | Claims set only (`eat_nonce`, `ueid`, `measurements`): `src/caps/uccs.c` |
| [`rfc9393.txt`](rfc9393.txt) | Concise Software Identification Tags (CoSWID) | RFC 9393 | The minimal evidence tag in `measurements`: `src/caps/uccs.c` |
| [`rfc9052.txt`](rfc9052.txt) | CBOR Object Signing and Encryption (COSE): Structures and Process | RFC 9052 | `COSE_Sign1_Tagged` and `Sig_structure`, detached payload: `src/core/suit.c`, `tools/suit-emit/` (signing is go-xkms's) |
| [`rfc9053.txt`](rfc9053.txt) | COSE: Initial Algorithms | RFC 9053 | ES256 (`-7`) identifier and raw r‖s form: `src/core/suit.c` |
| [`rfc9054.txt`](rfc9054.txt) | COSE: Hash Algorithms | RFC 9054 | SHA-256 (`-16`) identifier: `src/core/suit.c`, `tools/suit-emit/` |
| [`rfc5280.txt`](rfc5280.txt) | Internet X.509 PKI Certificate and CRL Profile | RFC 5280 | DER `TBSCertificate` encoding (§4.1) of the alias certificate the device mints: `src/caps/alias.c` (the certificate profile is go-xkms's) |
| [`rfc5758.txt`](rfc5758.txt) | Internet X.509 PKI: Additional Algorithms and Identifiers for DSA and ECDSA | RFC 5758 | `ecdsa-with-SHA256` AlgorithmIdentifier (§3.2): `src/caps/alias.c`, `src/caps/csr.c` |
| [`rfc2986.txt`](rfc2986.txt) | PKCS #10: Certification Request Syntax | RFC 2986 | `CertificationRequestInfo` (§4.1) of the possession CSR: `src/caps/csr.c` |
| [`rfc4043.txt`](rfc4043.txt) | Internet X.509 PKI Permanent Identifier | RFC 4043 | `id-on-permanentIdentifier` subjectAltName: `src/caps/csr.c` |
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

Each is a TCG specification; its licence does not allow a copy here.

## Board sources

Not standards: the vendor documents a board profile is checked against. Listed
by title and URL, because none publishes a licence that allows a copy.

| Document | Retrieved | URL | Checked against it |
|---|---|---|---|
| Waveshare, *ESP32-P4-WIFI6-POE-ETH Schematic* (3 pages; SHA-256 `885b8f038881c6656486fea143bcf749f17c6a1ec9006cc693dcdb298018380b`) | 2026-10-18 | <https://files.waveshare.com/wiki/ESP32-P4-WIFI6-POE-ETH/ESP32-P4-WIFI6-POE-ETH-Schematic.pdf> | Every pin and the flash part of `boards/esp32p4-poe/`: [`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md), "Pins" |
| Waveshare, *ESP32-P4-NANO Schematic* | 2026-10-18 | <https://files.waveshare.com/wiki/ESP32-P4-NANO/ESP32-P4-NANO-schematic.pdf> | Ruled out as the bench board: its flash is 16 MB |
