# ADR: the Cortex-M trust model, and its relation to TF-M

> **Status: PROPOSED (2026-10-15)** · Issue: #15 · Scope: trusted-firmware (the
> Cortex-M secure world and boot chain); go-xkms and go-quicbaas (what they must
> accept, seam only) · Related: #14,
> [`adr-reference-part-and-emulation.md`](adr-reference-part-and-emulation.md) ·
> Last verified: 2026-10-15
>
> Nothing here is implemented, and nothing is decided until the owner accepts it.
> The `cortex-m` row is gated behind the AVR physical end-to-end run (#7). This
> ADR changes no code, no wire vector and no SPEC row.

## The question

The SPEC row `cortex-m` asks for the Cortex-M7 and Cortex-M33 ports and an ADR on
"the trust model and its relation to TF-M". On AVR the trust model is ours from
the reset vector up: `tfw-boot` measures and verifies the application and derives
the layer-0 CDI, and the firmware builds and signs three fixed structures
(`docs/dice-attestation/architecture.md`). A Cortex-M part changes two things.
On an Armv8-M part (the M33) there is a hardware secure world, TrustZone-M, and an
existing open-source firmware for it: Trusted Firmware-M ("TF-M"), Arm's
reference implementation of the PSA specifications. The M7 (Armv7E-M) has no
TrustZone at all, only an MPU.

This ADR compares three trust models and recommends one for each core class:

- **(a)** our own minimal secure world on TrustZone-M: DICE layer 0 and the
  attestation commands in the secure image, the wire protocol in the non-secure
  image;
- **(b)** TF-M as the secure world, with our attestation code as a TF-M secure
  partition, or our firmware as a non-secure client of TF-M's PSA Crypto and
  Initial Attestation services;
- **(c)** no TrustZone: privileged and unprivileged code separated by the MPU.
  This is the only option the M7 has.

For each option it covers the code size, the attestation evidence against our
EAT/UCCS evidence, the licence, PSA Certified, the update path, and what L1 and
L2 say.

## Context

- **What we sign today, and the rule that protects it.** The DeviceID key signs
  exactly three structures: the 164-byte `ATTEST` evidence frame, the alias
  `TBSCertificate` carrying `tcg-dice-UCCS-evidence` (OID `2.23.133.5.4.6`),
  and the PKCS#10 `CertificationRequestInfo` for ACME finalize. **Only fixed
  templates with fixed-width host fields are ever signed**; no command signs a
  host-supplied digest (`docs/dice-attestation/architecture.md`, "What the
  DeviceID key signs"). Whatever secure world we choose has to keep that rule,
  or it becomes a signing oracle.
- **The UCCS claims we emit** are `nonce` (CWT key 10, 32 bytes), `ueid` (256,
  `0x01` followed by a 16-byte device id) and `measurements` (273, a CoSWID
  naming the firmware hash), in core-deterministic CBOR, inside an X.509 alias
  certificate signed ES256-style (ECDSA P-256, SHA-256) by the DeviceID key
  (`docs/dice-attestation/api.md`).
- **On the element tier the alias key is the DeviceID key.** The ATECC608 cannot
  turn a CDI into a P-256 key. The measurement is therefore bound by the
  signature over the claim, not by key derivation. A Cortex-M part with software
  or hardware ECC can derive a per-boot alias key from a CDI, which the AVR
  cannot.
- **Ownership.** This project owns "the device firmware, the secure world and
  on-device key handling" (SPEC, Purpose). go-xkms owns DICE ASN.1, EAT, CoRIM
  and the evidence-level verification. go-quicbaas owns the Verifier and the
  reference-value store (handbook `00-ownership-and-sdk-boundary.md`, L1). A gap
  in either is reported, not worked around.
- **The parts in view** (SPEC rows, and the #14 ADR,
  [`adr-reference-part-and-emulation.md`](adr-reference-part-and-emulation.md)).
  - The i.MX 95's Cortex-M7 is the reference part, and the first Cortex-M port
    (`imx95`; #14 decision 1). Its EdgeLock secure enclave holds the keys and AHAB
    verifies the M7 image. Its M33 runs NXP's System Manager, which stays NXP's.
  - The M7 runs from TCM and has no tfw-boot. Its layer 0 is NXP's ROM, enclave
    and System Manager (#14 decision 8), and #14 leaves the M7's CDI source to
    this ADR.
  - #14's CI target for the M7 is QEMU `mps2-an500`, and its M33 target, later,
    is `mps2-an505` (#14 decision 3).
  - The STM32H5 is the second target, and the first M33 (#14 decision 1). The
    M33 targets are the STM32H5 (`stm32h5`: the STM32H573I-DK with ST's Secure
    Manager, and the NUCLEO-H563ZI without it), the nRF54L15, the MCXN947 and
    the RP2350.
  - `hsm-self-defence` runs on the i.MX 95's M7.
- **Every M33 target has room.** The STM32H563 and STM32H573 carry 2 MB of flash
  and 640 KB of SRAM, the MCXN947 2 MB and 512 KB, the nRF54L15 1.5 MB of RRAM
  and 256 KB, and the RP2350 520 KB of SRAM with external flash. Flash is not
  the constraint it was on the 328P.

## Sources

Retrieved 2026-10-15. "Latest" pages of the TF-M documentation were checked
against the pinned v2.3.1 pages below.

| Source | Version | URL |
|---|---|---|
| TF-M releases and LTS tracker | v2.3.1, 2026-08-19; LTS 2.3 (first released 2026-04-28, maintained to April 2029) and LTS 2.1 (maintained to May 2027) | https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/releases/index.html |
| TF-M Profile Small | v2.3.1 | https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/configuration/profiles/tfm_profile_small.html |
| TF-M Profile Medium | v2.3.1 | https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/configuration/profiles/tfm_profile_medium.html |
| TF-M Initial Attestation integration guide | v2.3.1 | https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/integration_guide/services/tfm_attestation_integration_guide.html |
| TF-M adding a secure partition | v2.3.1 | https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/integration_guide/services/tfm_secure_partition_addition.html |
| TF-M builtin keys | v2.3.1 | https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/design_docs/tfm_builtin_keys.html |
| TF-M secure boot (BL2, MCUboot) | v2.3.1 | https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/design_docs/booting/tfm_secure_boot.html |
| TF-M firmware update service | v2.3.1 | https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/design_docs/services/tfm_fwu_service.html |
| TF-M platforms | v2.3.1 | https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/platform/index.html |
| TF-M licence | v2.3.1 (BSD-3-Clause) | https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/contributing/lic.html |
| TF-M security advisories | TFMV-1 to TFMV-14 at v2.3.1 | https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/security/security_advisories/index.html |
| tf-m-extras DICE Protection Environment partition | v2.3.1 (the "latest" page, which reports v2.3.1) | https://trustedfirmware-m.readthedocs.io/projects/tf-m-extras/en/latest/partitions/dice_protection_environment/dice_protection_environment.html |
| PSA Certified Attestation API (IHI 0085) | 2.0.0, May 2026 | https://arm-software.github.io/psa-api/attestation/2.0/ |
| PSA Certified Crypto API (IHI 0086) | 1.5.0 | https://arm-software.github.io/psa-api/crypto/1.5/ |
| PSA Certified Firmware Update API (IHI 0093) | 1.0.1 | https://arm-software.github.io/psa-api/fwu/1.0/ |
| PSA Certified Secure Storage API (IHI 0087) | 1.0.4 | https://arm-software.github.io/psa-api/storage/1.0/ |
| Arm Firmware Framework for M (FF-M, DEN 0063) | 1.0 | https://developer.arm.com/documentation/den0063/latest/ |
| FF-M 1.1 Extensions (AES 0039) | 1.1 | https://developer.arm.com/documentation/aes0039/latest |
| Platform Security Model (DEN 0128) | 1.1 | https://developer.arm.com/documentation/den0128/latest |
| RFC 9783, Arm's PSA Attestation Token | June 2025 | https://www.rfc-editor.org/rfc/rfc9783 |
| RFC 9711, Entity Attestation Token | | https://www.rfc-editor.org/rfc/rfc9711 |
| PSA Certified scheme (governed by GlobalPlatform since 2025, per IHI 0085 1.0.4) | | https://www.psacertified.org/ |
| MCUboot (Apache-2.0) | | https://docs.mcuboot.com/ |
| QEMU MPS2 and MPS3 boards | master | https://www.qemu.org/docs/master/system/arm/mps2.html |

## What TF-M is, in the terms that matter here

- **Profiles.** Profile Small uses the SFN model, isolation level 1 and
  symmetric crypto only. Its Initial Attestation is an HMAC-SHA256 `COSE_Mac0`,
  and it boots a single combined image. Profile Medium uses the IPC model,
  isolation level 2 (the PSA RoT is protected from the Application RoT) and
  ECDSA. Its Initial Attestation is asymmetric, and it boots multiple images with
  anti-rollback. Profile Large adds isolation level 3. A device-attest-01
  enrolment needs an asymmetric DeviceID key, so **Profile Medium is the
  smallest profile that fits our evidence.**
- **The token.** PSA Certified Attestation API 2.0.0 (May 2026) requires the
  report to be an RFC 9783 PSA attestation token. That token is a CBOR claims set
  in a tagged `COSE_Sign1` (or `COSE_Mac0` for symmetric keys) with no CWT tag.
  Its claims are:
  - `nonce`: key 10, 32, 48 or 64 bytes.
  - Instance ID: `ueid` key 256, 33 bytes, `0x01` followed by H(IAK).
  - Implementation ID: key 2396, 32 bytes.
  - Client ID: key 2394.
  - Security lifecycle: key 2395.
  - Boot seed: `bootseed` key 268.
  - Software components: key 2399, each with a measurement value and a signer ID.
  - Verification service indicator: key 2400.
  - Certification reference: key 2398.
  - `eat_profile`: `tag:psacertified.org,2023:psa#tfm` for TF-M.

  The TF-M profile says a receiver must accept ES256, ES384 and ES512 (and
  HMAC256/256 to HMAC512/512 for `COSE_Mac0`), and identifies the verification
  key by Instance ID.
- **Keys.** A builtin key is usable only by the callers its policy table names;
  a caller with no policy entry gets usage flags of zero (TF-M builtin keys).
  That is what lets a secure partition own a key that the non-secure side
  cannot use.
- **Boot and update.** BL2 is MCUboot. It verifies the secure and non-secure
  images, enforces security counters, and passes measurements to the attestation
  service. The firmware update service implements the PSA Firmware Update API.
- **DICE.** TF-M itself derives no DICE CDI. The DICE Protection Environment in
  tf-m-extras does derive CDIs and certificates, but it emits CWT certificates in
  the Open DICE profile (not X.509 with `tcg-dice-UCCS-evidence`), and it is
  built and tested on Arm's RSE platform.
- **Platforms upstream at v2.3.1.** STM32H573I-DK (not the NUCLEO-H563ZI),
  FRDM-MCXN947, LPCXpresso55S69, RP2350, nRF5340 and nRF9160/9161, and the
  MCIMX93-EVK (the i.MX 93, whose M33 is not taken by a System Manager). There
  is no nRF54L15 port upstream. Nordic carries TF-M for that part in its own SDK.

## The options

### (a) Our own minimal secure world on TrustZone-M

The secure image is ours and small:
- DICE layer 0: it measures the non-secure image, derives the CDI and holds the
  DeviceID key.
- The three template builders and the signing behind them.
- The boot decision: verify, anti-rollback, A/B.
- A handful of non-secure-callable veneers, one per attestation command, each
  taking the fixed-width nonce only.

The wire protocol, the capabilities and the application run non-secure, as they
do on AVR. The protocol parser is the largest attack surface in the firmware, so
it stays outside the trusted computing base; putting the whole wire protocol in
the secure image is rejected below.

- **Code size (estimated, unmeasured).** About 16–24 KB of flash and 2–4 KB of
  RAM for the secure image:
  - SHA-256 and HMAC: about 2–3 KB.
  - Software P-256 sign and verify: about 6–10 KB, near zero where the part has
    a PKA.
  - Templates, the UCCS encoder and the DER walker: about 3–4 KB.
  - SAU and MPC setup, the veneers and a secure fault handler: about 1–2 KB.
  - Boot verify, A/B and rollback: about 3–5 KB.
- **Evidence.** It emits the same three structures, unchanged. Because the M33
  can derive keys in software, the owner may choose a real per-boot alias key
  derived from the CDI. The chain then becomes a DeviceID certificate followed by
  the alias certificate (open question 5). **What go-xkms must accept:** the
  existing evidence frame, alias certificate and PKCS#10 from board ids
  `0x40`–`0x7F`, and, if the owner picks the derived alias key, a two-certificate
  DICE chain whose alias subject key differs from the DeviceID key. If the
  owner adds a new crypto tier value (open question 4), go-xkms must also accept
  it.
- **Licence.** All the code is ours, under AGPL-3.0-or-later and commercial. A
  crypto library we link instead of writing (L2) brings its own licence. The
  candidates are TF-PSA-Crypto (Apache-2.0 OR GPL-2.0-or-later) and the
  vendor's PKA driver.
- **PSA Certified.** Nothing is inherited. A chip's PSA Certified certificate
  covers the vendor's root of trust software, not a secure world we wrote. A
  certificate of our own would mean a lab evaluation of this code at Level 2 or
  above. Level 1 is a self-assessed questionnaire, and the PSA Functional API
  Certification tests the PSA APIs, which this option does not implement.
- **Update path.** The vendor ROM verifies our layer 0. Layer 0 sits in
  write-protected flash and is a bench update only. Layer 0 verifies the
  non-secure image (firmware and application) in A/B slots against the
  anti-rollback floor, as `tfw-boot` does on AVR, with the `ota` row's CoRIM
  manifest. The boot policy stays ours and the trailer format stays ours.
- **L1 and L2.** L1 is satisfied: one owner, us. L2 names in-house owners, and
  TF-M is not one, so L2 does not forbid this. Its intent does, though. A
  secure partition manager, the secure fault and veneer discipline, secure
  storage and an update service are a large, reviewed, security-maintained body
  of code with ports already upstream for three of our M33 targets. Writing a
  second one is the reimplementation L2 exists to stop. The SPEC already applies
  that rule to ESP-IDF's secure boot and OTA on the ESP32-P4: "composed and
  never re-implemented".

### (b) TF-M as the secure world

There are two shapes, and only one of them keeps the signing rule.

- **(b1) Our attestation code as a TF-M secure partition, the recommended shape.**
  - The partition is an Application RoT partition at isolation level 2 (Profile
    Medium, IPC model).
  - It holds the three template builders and nothing else.
  - The DeviceID key is a builtin key whose policy names our partition alone, or
    a key derived from the HUK with our partition id in the derivation. The
    non-secure side cannot use it.
  - The non-secure firmware calls the partition with `psa_call`, passing the
    command and the fixed-width nonce, and gets the paged structure back.
  - Measurements come from MCUboot's boot record.
  - The CDI, if the owner wants a derived alias key, is a KDF over the HUK and
    the non-secure image's measurement, computed in the partition before the
    non-secure image starts.
- **(b2) Our firmware as a non-secure client of PSA Crypto, rejected.** If the
  non-secure firmware holds a DeviceID key handle and calls `psa_sign_hash`,
  everything non-secure becomes a signing oracle for that key, and a bug in the
  wire protocol parser signs whatever an attacker wants. That breaks the one
  rule the attestation design rests on. Calling **Initial Attestation** from the
  non-secure side is safe: `psa_initial_attest_get_token` signs a fixed token
  over a 32, 48 or 64-byte challenge, which is the same shape as our rule. But
  that token is not an alias certificate or a PKCS#10, so it cannot complete
  device-attest-01 alone. It is an addition to (b1), never a replacement.

- **Code size (estimated, unmeasured).** On the order of 100 KB of flash and
  tens of KB of RAM for the TF-M secure image and the MCUboot BL2 together at
  Profile Medium. Our partition adds about 4–6 KB, because the crypto is TF-M's.
  TF-M's profile documents publish no footprint, so measuring it is follow-up F1.
  It fits every M33 target listed above with room to spare.
- **Evidence.** Our three structures come from the partition, unchanged, as in
  (a). Optionally the RFC 9783 PSA token rides beside them as a second piece of
  evidence, signed by TF-M's IAK. **What go-xkms must accept:**
  - Everything listed for (a).
  - If the owner wants the token: the RFC 9783 token under the `tfm` profile
    (tagged `COSE_Sign1`, ES256 at least, a 33-byte `ueid`, a 32–64-byte
    `nonce`, the software components claim), on go-xkms's EAT path.
  - The binding between the IAK's Instance ID and our DeviceID. Which side
    proves that binding is a design question for go-xkms, not for us.

  go-quicbaas's Verifier would need IAK endorsements and PSA reference values
  (implementation ID, software component measurements and signer IDs) in its
  store. The SPEC row `stm32h5` already assumes go-xkms's EAT path verifies a
  PSA token, from ST's Secure Manager. Whether it does today is unknown here and
  is reported, not checked by reading go-xkms.
- **Licence.** Every component is permissive, so all of them are compatible
  with AGPL-3.0-or-later and with our commercial licence:
  - TF-M and tf-m-extras: BSD-3-Clause.
  - MCUboot: Apache-2.0.
  - TF-PSA-Crypto: Apache-2.0 OR GPL-2.0-or-later.
  - QCBOR and t_cose: BSD-3-Clause.

  The obligations are notices: the BSD notices reproduced in the binary's
  documentation, and Apache's `NOTICE` files. Code we contribute upstream goes
  out under BSD-3-Clause with a DCO, so only generic fixes go upstream and our
  partition stays in this repository (open question 10). Vendor variants are
  different: ST's Secure Manager interface libraries, Nordic's SDK, and NXP's
  EdgeLock firmware and libraries. Each must be checked, at the evaluated
  version, for a field-of-use clause. That is the clause that ruled out
  CryptoAuthLib (`docs/attestation/cryptoauthlib-evaluation.md`, Finding 1).
- **PSA Certified.** This is the only option that keeps PSA Certified open:
  - A chip vendor certifies a chip together with a specific TF-M version and
    configuration as its PSA RoT.
  - A device built on it may cite that certificate (RFC 9783's certification
    reference claim exists for this) only while it runs that PSA RoT unchanged.
  - Whether adding our Application RoT partition at isolation level 2 leaves the
    certificate valid is a question for the certifying lab, not for this ADR
    (open question 9).
  - Device-level Level 1 is a questionnaire we could answer.
- **Update path.**
  - MCUboot BL2 verifies the secure and non-secure images as separate images,
    each with its own security counter.
  - The non-secure image (firmware and application) is our OTA unit, written
    through the PSA Firmware Update API from go-trusted-platform's OTA endpoint.
  - `tfw-boot` does not run on these parts. The boot policy is MCUboot's, and the
    image format is MCUboot's TLV trailer, signed with `imgtool`, not ours. Our
    `secure-boot` feature on this core class becomes MCUboot configuration.
  - That departs from #14, which expects the STM32H5 to be the second tfw-boot
    on Cortex-M, after the QEMU board. Under (b1), the Cortex-M tfw-boot stays
    on `mps2-an500`, and on M33 parts that take (a). #14's expectation holds
    only if the owner keeps (a) for the STM32H5 (open question 1).
  - The `ota` row says an image is accepted only if its measurement also matches
    the CoRIM manifest. MCUboot checks only its own signature, so that check
    moves either to the device (our partition, before it confirms the image) or
    to the Verifier at attestation (open question 6).
  - The TF-M secure image itself is updated on TF-M's cadence. TF-M has two LTS
    lines (2.1 to May 2027, 2.3 to April 2029) and fourteen advisories to date,
    and we would owe our fleet a release for each advisory that touches our
    configuration.
- **L1 and L2.** L1: the secure partition manager, isolation, crypto, secure
  storage, boot and update are TF-M's and MCUboot's. We own the partition, its
  manifest, the board integration and the non-secure firmware. That is one owner
  per capability, provided we carry **no local fork**: TF-M is consumed by
  release tag and checksum, as the vendoring gate does for vendored code today,
  and a fix TF-M needs goes upstream, as L2 says of an inadequate owner. L2: this
  is the option L2's intent points to.

### (c) No TrustZone: MPU-only isolation

The M7 has no secure state, so isolation is privilege plus the MPU:
- Privileged code holds the template builders, the policy-signature check (for
  `hsm-self-defence`) and the only mapping of the key store's interface. On the
  i.MX 95 that interface is the EdgeLock enclave's messaging unit.
- Layer 0 is not ours on the i.MX 95. It is NXP's ROM, enclave and System
  Manager (#14 decision 8). On an M7 with no SoC root of trust and with on-chip
  flash, layer 0 is tfw-boot, as on AVR.
- The wire protocol and the application run unprivileged, in MPU regions that
  exclude that interface, and reach the attestation commands through an SVC gate
  that takes the fixed-width nonce only.

The key never enters M7 memory, because the enclave holds it. What the MPU
protects is the right to *ask* the enclave to sign: without the MPU,
unprivileged code with a bug could drive the messaging unit directly and turn
the enclave into a signing oracle. Isolation from the A55 cores is the SoC's
domain access control, configured by NXP's System Manager, which
trusted-imx95 owns.

#14 leaves the MPU out of the first M7 port (its open question 7). That stays
compatible with this option only under one condition: the MPU and the SVC gate
must be in place before any M7 image links the enclave-backed attestation
commands. Until then the M7 image runs the `none` and `soft` crypto tiers,
which is what #14's QEMU target proves.

- **Code size (estimated, unmeasured).** About 6–12 KB of flash:
  - MPU setup, the SVC gate and a fault handler: about 1–3 KB.
  - The enclave messaging client: about 2–4 KB.
  - Templates and UCCS: about 3–4 KB.
  - No software ECC, because the enclave signs.

  An M7 without an enclave (an i.MX RT or STM32H7 board) takes the element tier
  instead, an ATECC608C or SE052F on its bus, exactly as AVR does.
- **Evidence.** The same three structures as (a). **What go-xkms must accept:**
  the same as (a). On top of that, the evidence must let a verifier tell MPU
  isolation from TrustZone isolation: a Cortex-M board id alone must not imply a
  secure world. How the evidence says so is ours (open question 4), and the
  policy that acts on it is go-quicbaas's. Whether go-xkms also has to accept
  the enclave's own attestation, as an endorsement of the DeviceID key, is open
  question 7.
- **Licence.** Our code only. The enclave's interface is NXP's, so its
  documentation and any NXP library we would link need the same field-of-use
  check as (b).
- **PSA Certified.** Not applicable to the M7 image: there is no PSA RoT on it.
  Any certification the i.MX 95 holds is NXP's and covers the enclave, not our
  M7 firmware.
- **Update path.** AHAB verifies the M7 image as a signed container before it
  runs. trusted-imx95 owns the boot container. The M7 image is our OTA unit, but
  who stages it is open (question 7 here, and #14's open question 5). Staging can come from the untrusted A55
  side because AHAB verifies at load, but the anti-rollback floor has to be
  AHAB's or the enclave's, not a value the A55 can rewrite.
- **L1 and L2.** L1: the M7 firmware is ours and the System Manager stays NXP's,
  so there is no overlap. L2: TF-M is not an option to reuse here. It needs
  TrustZone-M on the core it protects, or its multi-core topology with its
  secure processing environment on another core, and on the i.MX 95 that core is
  the System Manager's. What is reused is the enclave and AHAB, which is what
  the SPEC row `imx95` already says.

## Comparison

| | (a) own secure world | (b1) TF-M partition | (c) MPU-only |
|---|---|---|---|
| Cores | M33 | M33 | M7 (and any M33 fallback) |
| Secure image, flash (estimate) | 16–24 KB | ~100 KB with BL2, plus 4–6 KB ours | 6–12 KB |
| TCB we write | all of it | the partition | privileged layer and SVC gate |
| Our three structures | unchanged | unchanged | unchanged |
| PSA token (RFC 9783) | none | optional, from TF-M | none |
| Licence | ours | permissive inbound (BSD-3, Apache-2.0) | ours, plus NXP check |
| PSA Certified | nothing inherited | vendor certificate citable if the RoT is unchanged | not applicable |
| Boot and update | our boot policy, A/B | MCUboot and PSA FWU | AHAB container |
| L1 / L2 | L1 fine, against L2's intent | consistent with both | consistent with both |

## Recommendation

In #14's order: the reference part, the i.MX 95's M7, first; the M33 second.

- **Cortex-M7: (c), MPU-only.**
  - The template builders run privileged, behind an SVC gate.
  - Layer 0 is the SoC's where it has one (NXP's ROM, enclave and System Manager
    on the i.MX 95), and tfw-boot where it does not.
  - The key store is the SoC's enclave where there is one (the i.MX 95's
    EdgeLock), and the element tier where there is not.
  - Boot verification is the SoC's (AHAB on the i.MX 95).
  - `hsm-self-defence`'s policy-signature check and its actions on the enclave
    run privileged.
- **Cortex-M33: (b1), TF-M Profile Medium with our attestation code as an
  Application RoT secure partition.**
  - The wire protocol and the application run non-secure.
  - MCUboot is BL2.
  - TF-M is pinned to an LTS release (2.3 today), with no local fork.
  - Optionally, TF-M's PSA token is offered beside our three structures.

  Size does not decide on these parts, and reuse does: we would otherwise write
  and maintain a secure world that TF-M already maintains, with advisories,
  across ports that already exist for the STM32H573I-DK, the FRDM-MCXN947 and the
  RP2350. Where a part ships a vendor secure world (ST's Secure Manager on the
  STM32H573I-DK, Nordic's TF-M on the nRF54L15), the same partition targets the
  vendor's PSA-compatible secure world if the licence allows; otherwise it
  targets upstream TF-M. This is open question 2, and it is the `stm32h5` row's
  open decision, which this ADR does not close.
- **(a) is held in reserve, not chosen.** It is the fallback for an M33 part
  that TF-M has no port for and on which a port is not worth upstreaming. If the
  owner prefers (a) on the NUCLEO-H563ZI, as the `stm32h5` row's wording ("our
  own TrustZone secure world") suggests today, that is open question 1.
- **Language rule, proposed.** Never describe a device as "PSA Certified" unless
  it carries a certificate number, for the same reason the SPEC forbids "TCG
  DICE compliant".

## Wire consequences

None in this ADR. The attestation commands (`ATTEST` 0x60, `ATTEST_CERT` 0x63,
`ATTEST_CSR` 0x64), their nonce widths and every vector stay as they are on
every option. Two changes may follow from the owner's answers, and both extend
without renumbering:
- a crypto tier value for a key isolated by TrustZone or held in an SoC enclave
  (question 4);
- a second certificate in the alias chain (question 5).

## Rejected alternatives

- **(b2), our firmware as a PSA Crypto client holding the DeviceID key.** It is
  a signing oracle; see (b).
- **The whole wire protocol inside the secure image** (a literal reading of
  "DICE L0 and the wire protocol on TrustZone-M"). It puts the frame parser,
  the capabilities and the scheduler into the trusted computing base for no
  gain: only the three attestation commands need the key.
- **TF-M's DICE Protection Environment as our DICE.** It emits Open DICE CWT
  certificates rather than our X.509 alias with `tcg-dice-UCCS-evidence`, and it
  is built for Arm's RSE platform. Adopting it would change the evidence go-xkms
  verifies and give up the device-attest-01 alias. That could be a future
  evidence format, but not this ADR's.
- **Profile Small.** Symmetric attestation only; it cannot hold an asymmetric
  DeviceID key.
- **A local TF-M fork carrying our partition.** It breaks L1's "no local fork"
  and makes every TF-M advisory a merge. The partition is out of tree, which
  TF-M's out-of-tree partition support allows.

## Open questions for the owner

1. **M33 trust model.** Accept (b1), TF-M with our partition? If so, the
   `stm32h5` row's "our own TrustZone secure world" on the NUCLEO-H563ZI becomes
   "our partition on TF-M". TF-M upstream supports the STM32H573I-DK, not the
   NUCLEO-H563ZI, so that board would need a port, ideally upstreamed. Or keep
   (a) for the NUCLEO as the SPEC wording has it? (a) is also what #14's
   expectation of tfw-boot on the STM32H5 assumes.
2. **Vendor secure worlds.** On the STM32H573I-DK (ST Secure Manager) and the
   nRF54L15 (Nordic's TF-M), does our partition sit on the vendor's secure world
   or on upstream TF-M? This needs a licence check of each vendor's non-secure
   interface library against AGPL-3.0-or-later, as for CryptoAuthLib.
3. **The PSA token.** Do we offer RFC 9783 tokens beside our evidence? If yes,
   the coordinator files the gaps:
   - go-xkms: the PSA token on its EAT path, and the IAK-to-DeviceID binding.
   - go-quicbaas: IAK endorsements and PSA reference values in its store.

   Who provisions the IAK: the silicon vendor, or our bench through `tfwprov`?
4. **Crypto tier values.** Add `CAPS` tier values for "TrustZone-isolated key"
   and "SoC enclave key", extending past `TFW_CRYPTO_TPM2` (3), so a verifier can
   tell them from AVR's software and element tiers and from MPU-only isolation?
5. **The alias key on Cortex-M.** Derive a per-boot alias key from the CDI (a
   two-certificate chain, closer to TCG DICE layering), or keep the DeviceID key
   as the alias subject as on the element tier? go-xkms must accept whichever is
   chosen.
6. **Where the CoRIM check runs under MCUboot.** On the device, in our partition
   before the image is confirmed, or only at attestation in go-quicbaas's
   Verifier?
7. **The i.MX 95 M7.** These need trusted-imx95 and NXP's documentation:
   - Who stages M7 image updates?
   - Which mechanism holds the M7's anti-rollback floor (AHAB container version,
     or an enclave counter)?
   - Does the System Manager's configuration guarantee the A55 cannot halt,
     reset or write the M7? `hsm-self-defence` depends on it.
   - How many MPU regions does the M7 have? And does the MPU land in the first
     port, or only before the enclave-backed attestation commands (#14's open
     question 7)?
   - Can the enclave bind the M7's measurement into a key (a CDI), and should
     go-xkms accept the enclave's own attestation as an endorsement? #14
     decision 8 leaves the CDI source here.
8. **TF-M release policy.** Track LTS only (2.3, to April 2029), and what is
   our response time to a TF-M advisory?
9. **PSA Certified.** Pursue device-level certification? Ask a lab whether our
   Application RoT partition at isolation level 2 keeps a vendor's PSA RoT
   certificate citable? Adopt the language rule above?
10. **Upstream contributions.** Generic TF-M fixes go upstream under
    BSD-3-Clause with a DCO, while the partition stays under our dual licence.
    Is that acceptable?

## Follow-ups

- **F1.** Measure the footprint of TF-M v2.3.1 at Profile Medium with BL2, and
  of a stub partition, on QEMU `mps2-an505`. That is #14's later M33 emulation
  target, and TF-M has platform support for it. Measure the (c) privileged
  layer on #14's M7 target, `mps2-an500`. Replace every estimate in this ADR
  with the measured figure. F1 needs `mps2-an505` in CI, but this ADR does not
  (#14's open question 9 asks when it lands).
- **F2.** Once open question 3 is answered, the coordinator files the go-xkms
  and go-quicbaas gaps it names. This ADR touches neither repository.
- **F3.** When the owner accepts a model, update `docs/multi-arch/architecture.md`
  §6 and the `cortex-m` and `stm32h5` rows in one change.
- **F4.** The reference part and the CI emulation targets are #14's:
  [`adr-reference-part-and-emulation.md`](adr-reference-part-and-emulation.md).
  If the owner accepts (b1) for the STM32H5, that ADR's tfw-boot-on-STM32H5
  expectation needs the same update as F3.
