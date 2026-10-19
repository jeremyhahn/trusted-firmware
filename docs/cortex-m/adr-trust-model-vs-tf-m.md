# ADR: the Cortex-M trust model, and its relation to TF-M

> **Status: PROPOSED** · Issue: #15 · Scope: trusted-firmware (the
> Cortex-M secure world and boot chain); go-xkms and go-quicbaas (what they must
> accept, seam only) · Related: #14,
> [`adr-reference-part-and-emulation.md`](adr-reference-part-and-emulation.md) ·
> Last verified: 2026-10-18
>
> Nothing here is implemented, and nothing is decided until the owner accepts it.
> This ADR changes no code, no wire vector and no SPEC row.

## Changes on 2026-10-18 (owner, CA m623)

The owner ruled on 2026-10-18 (CA m623, 12:35Z) that #14 and this ADR are no
longer gated, and that both are decided against the Cortex-M parts on the bench
host: the STM32H573I-DK and the nRF54L15-DK, both Cortex-M33 with TrustZone-M.
The i.MX 95 M7 is a board of the port (#49), not its reference part, and the CI
target is QEMU `mps2-an505`. #14 carries the same ruling.

| Part of this ADR | What changed |
|---|---|
| The parts in view | the STM32H573I-DK is the reference board and the nRF54L15-DK the second; the i.MX 95 M7 is a board of the port; the CI target is `mps2-an505`, not `mps2-an500` |
| Where each secure world sits | new: one row per board, including the CI machine |
| What TF-M owns and what we own | new: the split, capability by capability (L1) |
| PSA interfaces | new: Attestation, Crypto and Firmware Update are implemented where they are the standard, and nothing is designed in their place (L2). The Firmware Update API is cited as the title page has it, 1.0 issue 1 |
| Recommendation | the M33 first, in #14's new order; the M7 second |
| Open questions | reordered: the M33 questions first, two new (3 and 11), the i.MX 95 M7 last (12) |
| F1 | TF-M v2.3.1 has no platform for `mps2-an505`; the footprint is measured on the reference board's build |

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
- **The parts in view** (SPEC rows, and #14,
  [`adr-reference-part-and-emulation.md`](adr-reference-part-and-emulation.md),
  after CA m623).
  - The reference part is the Cortex-M33 with TrustZone-M. The STM32H573I-DK is
    the reference board and the nRF54L15-DK the second (#14 decision 1).
  - The CI target is QEMU `mps2-an505`, a Cortex-M33 with TrustZone-M, and its
    image runs in the Secure state alone until this ADR is accepted (#14
    decision 3 and its question 2).
  - The i.MX 95's Cortex-M7 is a board of the port (#49). Its EdgeLock secure
    enclave (ELE) holds the keys and AHAB verifies the M7 image. Its M33 runs
    NXP's System Manager, which stays NXP's. The M7 runs from TCM and has no
    tfw-boot: its layer 0 is NXP's ROM, ELE and System Manager (#14 decision 8),
    and #14 leaves the M7's CDI source to this ADR.
  - The other M33 targets in the SPEC are the NUCLEO-H563ZI (the STM32H5 without
    ST's Secure Manager), the MCXN947 and the RP2350.
  - `hsm-self-defence` runs on the i.MX 95's M7.
- **Every M33 target has room.** The STM32H563 and STM32H573 carry 2 MB of flash
  and 640 KB of SRAM, the MCXN947 2 MB and 512 KB, the nRF54L15 1.5 MB of RRAM
  and 256 KB, and the RP2350 520 KB of SRAM with external flash. Flash is not
  the constraint it was on the 328P.
- **Board pages checked, 2026-10-16**
  ([`stm32h573i-dk.md`](../boards/stm32h573i-dk.md),
  [`rp2350-pico2w.md`](../boards/rp2350-pico2w.md),
  [`nrf54l15-dk.md`](../boards/nrf54l15-dk.md)). Three differences from the
  figures above, none of which changes the conclusion that flash is not the
  constraint:
  - **nRF54L15:** Nordic's datasheet gives the family's NVM as up to 1524 KB of
    RRAM (about 1.49 MiB, not 1.5 MB) and RAM up to 256 KB. The per-part figure
    was not confirmed from the section read.
  - **STM32H573:** 2 MB and 640 KB match ST's product page, but not the
    datasheet DS14053, which could not be retrieved. The figure is not confirmed.
  - **RP2350:** 520 KB of SRAM is confirmed (product brief RP-008374-DS-1).
    "External flash" is 4 MB on the Pico 2 W. The RP2350A has no internal flash,
    and the RP2354 variants carry 2 MB stacked.

  The facts bear on open questions 2 (Secure Manager, UM3254; Nordic's TF-M as
  the only nRF54L15 secure world) and 5 (the RP2350's OTP key store has no
  public-key accelerator; the nRF54L15's KMU hides keys from secure code).

## What TF-M is, in the terms that matter here

TF-M pages are cited at v2.3.1, retrieved 2026-10-15, by path under
<https://trustedfirmware-m.readthedocs.io/en/tf-mv2.3.1/>. "Latest" pages were
checked against those pinned pages.

- **Profiles.** Profile Small uses the SFN model, isolation level 1 and
  symmetric crypto only. Its Initial Attestation is an HMAC-SHA256 `COSE_Mac0`,
  and it boots a single combined image. Profile Medium uses the IPC model,
  isolation level 2 (the PSA RoT is protected from the Application RoT) and
  ECDSA. Its Initial Attestation is asymmetric, and it boots multiple images with
  anti-rollback. Profile Large adds isolation level 3. A device-attest-01
  enrolment needs an asymmetric DeviceID key, so **Profile Medium is the
  smallest profile that fits our evidence.** (`configuration/profiles/tfm_profile_small.html`,
  `configuration/profiles/tfm_profile_medium.html`.)
- **The token.** PSA Certified Attestation API 2.0 (IHI 0085, issue 0, May 2026,
  <https://arm-software.github.io/psa-api/attestation/2.0/>), §3, requires the
  report to be an RFC 9783 PSA attestation token
  (<https://www.rfc-editor.org/rfc/rfc9783>, June 2025). That token is a CBOR claims set
  in a tagged `COSE_Sign1` (or `COSE_Mac0` for symmetric keys) with no CWT tag.
  Its claims (RFC 9783 §4) are:
  - `nonce`: key 10, 32, 48 or 64 bytes.
  - Instance ID: `ueid` key 256, 33 bytes, `0x01` followed by H(IAK).
  - Implementation ID: key 2396, 32 bytes.
  - Client ID: key 2394.
  - Security lifecycle: key 2395.
  - Boot seed: `bootseed` key 268.
  - Software components: key 2399, each with a measurement value and a signer ID.
  - Verification service indicator: key 2400.
  - Certification reference: key 2398.
  - `eat_profile`: `tag:psacertified.org,2023:psa#tfm` for TF-M (RFC 9783 §5.2,
    `integration_guide/services/tfm_attestation_integration_guide.html`).

  The TF-M profile says a receiver must accept ES256, ES384 and ES512 (and
  HMAC256/256 to HMAC512/512 for `COSE_Mac0`), and identifies the verification
  key by Instance ID. RFC 9783 is an Independent Submission, Informational, and
  says itself that it is not an IETF standard; it is cited because the PSA
  Attestation API names it as the report format.
- **Keys.** A builtin key is usable only by the callers its policy table names;
  a caller with no policy entry gets usage flags of zero
  (`design_docs/tfm_builtin_keys.html`).
  That is what lets a secure partition own a key that the non-secure side
  cannot use.
- **Boot and update.** BL2 is MCUboot (<https://docs.mcuboot.com/>). It verifies
  the secure and non-secure images, enforces security counters, and passes
  measurements to the attestation service (`design_docs/booting/tfm_secure_boot.html`).
  The firmware update service (`design_docs/services/tfm_fwu_service.html`)
  implements the PSA Certified Firmware Update API 1.0 (IHI 0093, issue 1,
  23 September 2025, <https://arm-software.github.io/psa-api/fwu/1.0/>).
- **DICE.** TF-M itself derives no DICE CDI. The DICE Protection Environment in
  tf-m-extras does derive CDIs and certificates, but it emits CWT certificates in
  the Open DICE profile (not X.509 with `tcg-dice-UCCS-evidence`), and it is
  built and tested on Arm's RSE platform
  (<https://trustedfirmware-m.readthedocs.io/projects/tf-m-extras/en/latest/partitions/dice_protection_environment/dice_protection_environment.html>,
  which reports v2.3.1).
- **Platforms upstream at v2.3.1** (`platform/index.html`, re-read 2026-10-18).
  STM32H573I-DK (not the NUCLEO-H563ZI), FRDM-MCXN947, LPCXpresso55S69, RP2350,
  nRF5340 and nRF9160/9161, and the MCIMX93-EVK (the i.MX 93, whose M33 is not
  taken by a System Manager). There is no nRF54L15 port upstream; Nordic carries
  TF-M for that part in its own SDK. There is no MPS2 platform either, so none
  for QEMU `mps2-an505`: the Arm reference platforms listed are Corstone-300,
  Corstone-310 and Alcor FPGA and FVP builds.

## Where each secure world sits

| Board | Core | Secure world | Where it sits | What is ours |
|---|---|---|---|---|
| STM32H573I-DK (reference board) | M33, TrustZone-M | ST's Secure Manager, or upstream TF-M (open question 2) | the Secure state of the same core | an Application RoT partition and the non-secure firmware |
| nRF54L15-DK (second board) | M33, TrustZone-M | Nordic's TF-M from its SDK; keys in the KMU | the Secure state of the same core | the same partition and non-secure firmware |
| QEMU `mps2-an505` (CI) | M33, TrustZone-M | none until this ADR is accepted; TF-M v2.3.1 has no platform for it (open question 11) | the whole image runs Secure (#14 question 2) | the whole image |
| i.MX 95 M7 (board of the port, #49) | M7, no TrustZone | none on the core; the ELE holds the keys, AHAB verifies, the System Manager on the M33 sets domain access | off the core: the ELE and the M33, both NXP's, configured by trusted-imx95 | the privileged layer, the SVC gate and the unprivileged firmware (option (c)) |

On both bench boards the secure world is on the same core as our firmware, one
state boundary away, so the question is who writes the Secure image. On the
i.MX 95 there is no Secure state on the M7 at all, and the root of trust is on
other silicon that NXP owns.

## What TF-M owns, and what this project owns

Under the recommended option (b1), one owner per capability (L1):

| Capability | Owner |
|---|---|
| Secure partition manager, isolation, IPC (FF-M) | TF-M |
| Crypto, key store and builtin-key policy (PSA Crypto API) | TF-M (TF-PSA-Crypto inside it) |
| Initial Attestation token (PSA Attestation API, RFC 9783) | TF-M |
| Firmware update service (PSA FWU API) | TF-M |
| BL2: image verification, security counters, A/B | MCUboot |
| The vendor RoT on parts that ship one | ST (Secure Manager), Nordic (its SDK's TF-M) |
| The three template builders, and the partition that holds them | this project |
| The partition manifest and the board integration | this project |
| The non-secure firmware: wire protocol, capabilities, application | this project |
| DICE ASN.1, EAT, CoRIM, evidence verification | go-xkms |
| Verifier and reference values | go-quicbaas |

TF-M is consumed by release tag and checksum, with no local fork. A fix TF-M
needs goes upstream (open question 10).

## PSA interfaces: implemented where they are the standard

Where TF-M or a vendor's PSA RoT is the secure world, the PSA Certified APIs are
the interface to it. We call them and design nothing in their place (L2;
standards first, handbook `documentation.md` §9). On the M7 there is no PSA RoT
and none of them applies; the ELE's interface is NXP's.

- **Attestation.** PSA Certified Attestation API 2.0 (IHI 0085, issue 0, May
  2026, <https://arm-software.github.io/psa-api/attestation/2.0/>).
  - §4.5.1, `psa_initial_attest_get_token`, is called with a challenge of one of
    the §4.4 sizes (32, 48 or 64 bytes). TF-M implements it; we never sign a
    token of our own.
  - §3 fixes the report as an RFC 9783 token. Its claims (RFC 9783 §4) and the
    TF-M profile (§5.2) are the token's whole format; we add no claim.
  - Verification is RFC 9783 §8, and it is go-xkms's and go-quicbaas's.
  - Whether the token is offered at all is open question 4.
- **Crypto.** PSA Certified Crypto API 1.5 (IHI 0086, issue 0, June 2026,
  <https://arm-software.github.io/psa-api/crypto/1.5/>), used inside our
  partition only.
  - The signature behind the three templates is `psa_sign_hash` (§10.9.8) with
    ECDSA (§10.9.2), over a SHA-256 digest (§10.2).
  - The DeviceID key is a builtin or persistent key (§9.6) whose policy (§9.8)
    names our partition alone.
  - A derived alias key, if the owner chooses one, comes from §10.8, key
    derivation.
  - Our own SHA-256 and P-256 code is not linked into an M33 secure image; TF-M's
    is. The non-secure side never holds a key handle ((b2), rejected).
- **Firmware update.** PSA Certified Firmware Update API 1.0 (IHI 0093, issue 1,
  23 September 2025, <https://arm-software.github.io/psa-api/fwu/1.0/>).
  - The deployment is §3.3.1, untrusted client: the non-secure firmware is the
    update client and TF-M's FWU service is the update service.
  - Our OTA transport delivers the image to the update client, which drives the
    §4.5 sequence through the §5.6 functions: prepare, install, test (the trial)
    and accept.
  - The bootloader side (§4.6) is MCUboot's. tfw-boot does not run on these
    boards.
  - How each OTA command maps onto §5.6 is the implementing change's to state,
    not this ADR's.

None of these documents is copied into `docs/reference/` with this ADR. Each is
cited here by title, document number, issue and URL. Its copy, or under a licence
that forbids one its title and URL, lands in `docs/reference/` with the change
that implements the interface.

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
  the alias certificate (open question 6). **What go-xkms must accept:** the
  existing evidence frame, alias certificate and PKCS#10 from board ids
  `0x40`–`0x7F`, and, if the owner picks the derived alias key, a two-certificate
  DICE chain whose alias subject key differs from the DeviceID key. If the
  owner adds a new crypto tier value (open question 5), go-xkms must also accept
  it.
- **Licence.** All the code is ours, under AGPL-3.0-or-later and commercial. A
  crypto library we link instead of writing (L2) brings its own licence. The
  candidates are TF-PSA-Crypto (Apache-2.0 OR GPL-2.0-or-later) and the
  vendor's PKA driver.
- **PSA Certified** (<https://www.psacertified.org/>, governed by GlobalPlatform
  since 2025). Nothing is inherited. A chip's PSA Certified certificate
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
  storage (PSA Secure Storage API, IHI 0087 1.0.4,
  <https://arm-software.github.io/psa-api/storage/1.0/>) and an update service
  are a large, reviewed, security-maintained body
  of code with ports already upstream for three of our M33 targets, the
  reference board among them. Writing a second one is the reimplementation L2
  exists to stop, and it would design a firmware update interface where PSA FWU
  is the standard. The SPEC already applies that rule to ESP-IDF's secure boot
  and OTA on the ESP32-P4: "composed and never re-implemented".

### (b) TF-M as the secure world

There are two shapes, and only one of them keeps the signing rule.

- **(b1) Our attestation code as a TF-M secure partition, the recommended shape.**
  - The partition is an Application RoT partition at isolation level 2 (Profile
    Medium, IPC model; FF-M, DEN 0063 1.0,
    <https://developer.arm.com/documentation/den0063/latest/>, and its 1.1
    Extensions, AES 0039, <https://developer.arm.com/documentation/aes0039/latest>).
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
  non-secure firmware holds a DeviceID key handle and calls `psa_sign_hash`
  (PSA Certified Crypto API 1.5, IHI 0086 issue 0, §10.9.8,
  <https://arm-software.github.io/psa-api/crypto/1.5/>),
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
    `nonce`, the software components claim), on go-xkms's EAT path (RFC 9711,
    <https://www.rfc-editor.org/rfc/rfc9711>).
  - The binding between the IAK's Instance ID and our DeviceID. Which side
    proves that binding is a design question for go-xkms, not for us.

  go-quicbaas's Verifier would need IAK endorsements and PSA reference values
  (implementation ID, software component measurements and signer IDs) in its
  store. The SPEC row `stm32h5` already assumes go-xkms's EAT path verifies a
  PSA token, from ST's Secure Manager. Whether it does today is unknown here and
  is reported, not checked by reading go-xkms.
- **Licence.** Every component is permissive, so all of them are compatible
  with AGPL-3.0-or-later and with our commercial licence:
  - TF-M and tf-m-extras: BSD-3-Clause (`contributing/lic.html`).
  - MCUboot: Apache-2.0 (<https://docs.mcuboot.com/>).
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
    configuration as its PSA RoT (Platform Security Model, DEN 0128 1.1,
    <https://developer.arm.com/documentation/den0128/latest>).
  - A device built on it may cite that certificate (RFC 9783's certification
    reference claim, §4.2.3, exists for this) only while it runs that PSA RoT
    unchanged.
  - Whether adding our Application RoT partition at isolation level 2 leaves the
    certificate valid is a question for the certifying lab, not for this ADR
    (open question 9).
  - Device-level Level 1 is a questionnaire we could answer.
- **Update path.**
  - MCUboot BL2 verifies the secure and non-secure images as separate images,
    each with its own security counter.
  - The non-secure image (firmware and application) is our OTA unit, written
    through the PSA Firmware Update API (see "PSA interfaces") from
    go-trusted-platform's OTA endpoint.
  - `tfw-boot` does not run on these parts. The boot policy is MCUboot's, and the
    image format is MCUboot's TLV trailer, signed with `imgtool`, not ours. Our
    `secure-boot` feature on this core class becomes MCUboot configuration.
  - #14 leaves tfw-boot on the M33 boards to this ADR (its HAL table: "depends
    on #15's model"). Under (b1), tfw-boot runs on neither bench board; it stays
    on `mps2-an505` while that image runs Secure alone, and on M33 parts that
    take (a) (open question 1).
  - The `ota` row says an image is accepted only if its measurement also matches
    the CoRIM manifest. MCUboot checks only its own signature, so that check
    moves either to the device (our partition, before it accepts the image) or
    to the Verifier at attestation (open question 7).
  - The TF-M secure image itself is updated on TF-M's cadence. TF-M has two LTS
    lines (2.1 to May 2027, 2.3 to April 2029; v2.3.1 is of 2026-08-19,
    `releases/index.html`) and fourteen advisories to date (TFMV-1 to TFMV-14,
    `security/security_advisories/index.html`), and we would owe our fleet a release for each advisory that touches our
    configuration.
- **L1 and L2.** L1: the secure partition manager, isolation, crypto, secure
  storage, boot and update are TF-M's and MCUboot's (see "What TF-M owns").
  That is one owner per capability, provided we carry **no local fork**: TF-M is
  consumed by release tag and checksum, as the vendoring gate does for vendored
  code today, and a fix TF-M needs goes upstream, as L2 says of an inadequate
  owner. L2: this is the option L2's intent points to.

### (c) No TrustZone: MPU-only isolation

The M7 has no secure state, so isolation is privilege plus the MPU:
- Privileged code holds the template builders, the policy-signature check (for
  `hsm-self-defence`) and the only mapping of the key store's interface. On the
  i.MX 95 that interface is the ELE's messaging unit.
- Layer 0 is not ours on the i.MX 95. It is NXP's ROM, ELE and System Manager
  (#14 decision 8). On an M7 with no SoC root of trust and with on-chip flash,
  layer 0 is tfw-boot, as on AVR.
- The wire protocol and the application run unprivileged, in MPU regions that
  exclude that interface, and reach the attestation commands through an SVC gate
  that takes the fixed-width nonce only.

The key never enters M7 memory, because the enclave holds it. What the MPU
protects is the right to *ask* the enclave to sign: without the MPU,
unprivileged code with a bug could drive the messaging unit directly and turn
the enclave into a signing oracle. Isolation from the A55 cores is the SoC's
domain access control, configured by NXP's System Manager, which
trusted-imx95 owns.

#14 leaves the MPU out of the first port (its question 5). That stays
compatible with this option only under one condition: the MPU and the SVC gate
must be in place before any M7 image links the enclave-backed attestation
commands. Until then the M7 image runs the `none` and `soft` crypto tiers.

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
  secure world. How the evidence says so is ours (open question 5), and the
  policy that acts on it is go-quicbaas's. Whether go-xkms also has to accept
  the enclave's own attestation, as an endorsement of the DeviceID key, is open
  question 12.
- **Licence.** Our code only. The enclave's interface is NXP's, so its
  documentation and any NXP library we would link need the same field-of-use
  check as (b).
- **PSA Certified.** Not applicable to the M7 image: there is no PSA RoT on it.
  Any certification the i.MX 95 holds is NXP's and covers the enclave, not our
  M7 firmware.
- **Update path.** AHAB verifies the M7 image as a signed container before it
  runs. trusted-imx95 owns the boot container. The M7 image is our OTA unit, but
  who stages it is open (question 12 here, and #14's question 9). Staging can come from the untrusted A55
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
| Boards (CA m623) | a fallback only | STM32H573I-DK, nRF54L15-DK | i.MX 95 M7 |
| Secure image, flash (estimate) | 16–24 KB | ~100 KB with BL2, plus 4–6 KB ours | 6–12 KB |
| TCB we write | all of it | the partition | privileged layer and SVC gate |
| Our three structures | unchanged | unchanged | unchanged |
| PSA token (RFC 9783) | none | optional, from TF-M | none |
| PSA APIs | none | Attestation 2.0, Crypto 1.5, FWU 1.0 | none |
| Licence | ours | permissive inbound (BSD-3, Apache-2.0) | ours, plus NXP check |
| PSA Certified | nothing inherited | vendor certificate citable if the RoT is unchanged | not applicable |
| Boot and update | our boot policy, A/B | MCUboot and PSA FWU | AHAB container |
| L1 / L2 | L1 fine, against L2's intent | consistent with both | consistent with both |

## Recommendation

In #14's order after CA m623: the Cortex-M33 with TrustZone-M first, on the
reference board; the i.MX 95's M7 as a board of the port.

- **Cortex-M33: (b1), TF-M Profile Medium with our attestation code as an
  Application RoT secure partition**, on the STM32H573I-DK and the nRF54L15-DK.
  - The wire protocol and the application run non-secure.
  - MCUboot is BL2.
  - The PSA Attestation, Crypto and Firmware Update APIs are the interfaces to
    the secure world, as "PSA interfaces" lists; nothing is designed in their
    place.
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
- **Cortex-M7: (c), MPU-only.**
  - The template builders run privileged, behind an SVC gate.
  - Layer 0 is the SoC's where it has one (NXP's ROM, ELE and System Manager
    on the i.MX 95), and tfw-boot where it does not.
  - The key store is the SoC's enclave where there is one (the i.MX 95's
    ELE), and the element tier where there is not.
  - Boot verification is the SoC's (AHAB on the i.MX 95).
  - `hsm-self-defence`'s policy-signature check and its actions on the enclave
    run privileged.
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
every option. Three changes may follow from the owner's answers, and each
extends without renumbering:
- a crypto tier value for a key isolated by TrustZone or held in an SoC enclave
  (question 5);
- a second certificate in the alias chain (question 6);
- a command that returns the PSA token, if the owner wants it offered
  (question 4).

## Rejected alternatives

- **(b2), our firmware as a PSA Crypto client holding the DeviceID key.** It is
  a signing oracle; see (b).
- **The whole wire protocol inside the secure image** (a literal reading of
  "DICE L0 and the wire protocol on TrustZone-M"). It puts the frame parser,
  the capabilities and the scheduler into the trusted computing base for no
  gain: only the three attestation commands need the key.
- **An update or attestation interface of our own beside TF-M's.** PSA FWU and
  PSA Attestation are the standard interfaces to the secure world; a second one
  is the reimplementation L2 forbids.
- **TF-M's DICE Protection Environment as our DICE.** It emits Open DICE CWT
  certificates rather than our X.509 alias with `tcg-dice-UCCS-evidence`, and it
  is built for Arm's RSE platform. Adopting it would change the evidence go-xkms
  verifies and give up the device-attest-01 alias. That could be a future
  evidence format, but not this ADR's.
- **Profile Small.** Symmetric attestation only; it cannot hold an asymmetric
  DeviceID key.
- **A local TF-M fork carrying our partition.** It breaks L1's "no local fork"
  and makes every TF-M advisory a merge. The partition is out of tree, which
  TF-M's out-of-tree partition support allows
  (`integration_guide/services/tfm_secure_partition_addition.html`).

## Open questions for the owner

1. **M33 trust model.** Accept (b1), TF-M with our partition, on the
   STM32H573I-DK and the nRF54L15-DK? If so, the `stm32h5` row's "our own
   TrustZone secure world" on the NUCLEO-H563ZI becomes "our partition on
   TF-M". TF-M upstream supports the STM32H573I-DK, not the NUCLEO-H563ZI, so
   that board would need a port, ideally upstreamed. Or keep (a) for the NUCLEO
   as the SPEC wording has it?
2. **Vendor secure worlds.** On the STM32H573I-DK (ST Secure Manager) and the
   nRF54L15-DK (Nordic's TF-M), does our partition sit on the vendor's secure
   world or on upstream TF-M? This needs a licence check of each vendor's
   non-secure interface library against AGPL-3.0-or-later, as for CryptoAuthLib.
3. **The PSA interfaces.** On every board with a PSA RoT, are PSA Attestation
   2.0, PSA Crypto 1.5 (inside our partition only) and PSA Firmware Update 1.0
   the interfaces, with our OTA commands carried onto FWU's untrusted-client
   sequence and MCUboot in place of tfw-boot? Does each document's copy land in
   `docs/reference/` with the change that implements it, not before?
4. **The PSA token.** Do we offer RFC 9783 tokens beside our evidence? If yes,
   the coordinator files the gaps:
   - go-xkms: the PSA token on its EAT path, and the IAK-to-DeviceID binding.
   - go-quicbaas: IAK endorsements and PSA reference values in its store.

   Who provisions the IAK: the silicon vendor, or our bench through `tfwprov`?
5. **Crypto tier values.** Add `CAPS` tier values for "TrustZone-isolated key"
   and "SoC enclave key", extending past `TFW_CRYPTO_TPM2` (3), so a verifier can
   tell them from AVR's software and element tiers and from MPU-only isolation?
6. **The alias key on Cortex-M.** Derive a per-boot alias key from the CDI (a
   two-certificate chain, closer to TCG DICE layering), or keep the DeviceID key
   as the alias subject as on the element tier? go-xkms must accept whichever is
   chosen.
7. **Where the CoRIM check runs under MCUboot.** On the device, in our partition
   before the image is accepted, or only at attestation in go-quicbaas's
   Verifier?
8. **TF-M release policy.** Track LTS only (2.3, to April 2029), and what is
   our response time to a TF-M advisory?
9. **PSA Certified.** Pursue device-level certification? Ask a lab whether our
   Application RoT partition at isolation level 2 keeps a vendor's PSA RoT
   certificate citable? Adopt the language rule above?
10. **Upstream contributions.** Generic TF-M fixes go upstream under
    BSD-3-Clause with a DCO, while the partition stays under our dual licence.
    Is that acceptable?
11. **The secure world in CI.** TF-M v2.3.1 has no platform for `mps2-an505`.
    Do we write one, out of tree and offered upstream, so `make ci` runs (b1)
    under QEMU? Or does the CI image stay Secure-only (#14 question 2), with
    (b1) proven on the bench boards alone?
12. **The i.MX 95 M7.** These need trusted-imx95 and NXP's documentation:
    - Who stages M7 image updates?
    - Which mechanism holds the M7's anti-rollback floor (AHAB container version,
      or an enclave counter)?
    - Does the System Manager's configuration guarantee the A55 cannot halt,
      reset or write the M7? `hsm-self-defence` depends on it.
    - How many MPU regions does the M7 have? And does the MPU land in the first
      port, or only before the enclave-backed attestation commands (#14's
      question 5)?
    - Can the enclave bind the M7's measurement into a key (a CDI), and should
      go-xkms accept the enclave's own attestation as an endorsement? #14
      decision 8 leaves the CDI source here.

    Facts gathered for the M7's OTA (#52; the full table is decision
    12 of [the OTA ADR](../ota/adr-ota-image-format-and-trust.md)). The question
    stays open. The device is the NXP FRDM-IMX95-PRO (UM12527). Facts were
    checked against UM12527 Rev. 1.0, RM00284 Rev. 4.5 and IMX95CEC Rev. 8.
    - **Verified:** the board has up to six Cortex-A55, one Cortex-M7 and one
      Cortex-M33 (UM12527 §1.2 Table 1). For the family, the M7 runs at up to
      800 MHz with 512 KB of TCM (IMX95CEC Rev. 8, Table 1).
    - **Verified:** memory is a 16 GB LPDDR5, a 32 GB eMMC 5.1 on uSDHC1 (the
      default boot device), microSD on uSDHC2, and a 512 Mbit Winbond
      W25Q512NWEIQ NOR on XSPI (UM12527 §1.2, §2.6, §2.8–§2.10). On SW4, FlexSPI
      NOR is `x100` (UM12527 §2.3 Table 7). The four debug UARTs on J22 have no
      fixed core mapping (UM12527 §2.19).
    - **Verified:** `hsm_commit()` with `HSM_COMMIT_OEM_FW_VERSION` fuses the
      OEM container's firmware version (RM00284 §3.35). The ELE's monotonic
      counter protects only key-store exports made under `STRICT_OPERATION`
      with the monotonic flag (§2.4.2). Its fuse words are assigned once per
      device by `hsm_dev_mono_counter()` (§3.24). There is no general counter
      for software. Data storage sits in the Linux filesystem, with no strict
      or monotonic flag and no roll-back claim (§3.8).
    - **Verified:** the ELE boot mode (`crrm_set_boot_mode()`) is a recovery
      path, not a trial-boot pointer (RM00284 §3.25). On eMMC, the boot pointer
      is `BOOT_PARTITION_ENABLE` (JEDEC JESD84-B51).
    - **Public, outside the set:** the AHAB `fuse_version` (8-bit floor) and
      `sw_version` (16-bit, not a floor) come from SPSDK and U-Boot
      `doc/imx/imx95_container.txt`. So does the M7 as `cortex_m7_app` in the
      primary container set, sharing its fuse version, from SPSDK *i.MX 95 AHAB
      with U-Boot*. The ROM's secondary boot comes from the U-Boot patch
      *imx9: scmi: Support iMX95/94/952 secondary boot*, 2026-04.
    - **Needed:**
      - from the i.MX 95 Security Reference Manual: the fuse word behind the OEM
        fuse version and its steps, and the number of monotonic-counter fuse
        words;
      - from IMX95RM, AHAB chapter: the container fields;
      - from IMX95RM, System Boot chapter: what triggers the ROM fallback, the
        secondary-offset fuse, whether software can force a one-time secondary
        boot, and whether the ROM boots the enabled eMMC partition;
      - from IMX95RM: whether the M7 can reach an ELE messaging unit.

## Follow-ups

- **F1.** Measure the footprint of TF-M v2.3.1 at Profile Medium with BL2, and
  of a stub partition, from a build for the STM32H573I-DK, the reference board
  and an upstream TF-M platform; the linker map needs no board. TF-M v2.3.1 has
  no platform for QEMU `mps2-an505`
  (<https://www.qemu.org/docs/master/system/arm/mps2.html>), so the emulator
  carries the measurement only if open question 11 is answered yes. Measure the
  (c) privileged layer in the M7 build (`mps2-an500` is #14's question 8).
  Replace every estimate in this ADR with the measured figure.
- **F2.** Once open question 4 is answered, the coordinator files the go-xkms
  and go-quicbaas gaps it names. This ADR touches neither repository.
- **F3.** When the owner accepts a model, update `docs/multi-arch/architecture.md`
  §6 and the `cortex-m` and `stm32h5` rows in one change.
- **F4.** The reference part and the CI emulation target are #14's:
  [`adr-reference-part-and-emulation.md`](adr-reference-part-and-emulation.md).
  If the owner accepts (b1), #14's HAL-table row for tfw-boot on the M33 boards
  ("depends on #15's model") becomes "none; BL2 is MCUboot" in the same change as
  F3.
