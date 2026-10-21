# DICE attestation — engineering

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

See `architecture.md` for design and boundaries; not restated here.

## Exported surface

### `include/tfw/crypto.h` — the provider contract, substituted at link time

| Symbol | Signature | Note |
|---|---|---|
| `tfw_crypto_init` | `tfw_status_t(void)` | derives the CDI if a UDS is present; never errors on an unprovisioned device |
| `tfw_crypto_tier` | `tfw_crypto_tier_t(void)` | `TFW_CRYPTO_NONE`\|`SOFTWARE`\|`SECURE_ELEMENT`\|`TPM2` = 0/1/2/3 |
| `tfw_crypto_algorithms` | `uint16_t(void)` | bitmap: `TFW_ALGBIT_ECDSA_P256_SHA256`\|`ED25519`\|`HMAC_SHA256` |
| `tfw_crypto_random` | `tfw_status_t(uint8_t *out, uint8_t len)` | `TFW_ERR_CRYPTO` on a tier with no entropy source |
| `tfw_crypto_sha256` | `tfw_status_t(const uint8_t *data, uint16_t len, uint8_t out[32])` | |
| `tfw_crypto_hmac` | `tfw_status_t(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32])` | the DICE one-way function; `slot` never crosses the bus |
| `tfw_crypto_sign` | `tfw_status_t(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id)` | writes the algorithm actually used |
| `tfw_crypto_pubkey` | `tfw_status_t(uint8_t out[64])` | `TFW_ERR_NOT_PROVISIONED` or `TFW_ERR_UNSUPPORTED` (symmetric tier) |
| `tfw_crypto_counter` | `tfw_status_t(uint8_t idx, uint32_t *out)` | monotonic; backs `BootCount` and rollback |

### `src/caps/attest.h` / `src/caps/alias.h` / `src/caps/csr.h` / `src/caps/tmpl.h` / `src/caps/uccs.h`

| Symbol | Note |
|---|---|
| `tfw_attest_cmd(tfw_reader_t *, tfw_writer_t *)` | dispatch handler for `ATTEST` |
| `tfw_attest_device_id()` / `tfw_attest_fw_hash()` | pointers into the evidence buffer, `TFW_ATTEST_ID_LEN`/`TFW_SHA256_LEN` bytes; zero until the bootloader handoff lands (`architecture.md`) |
| `tfw_attest_reset_state(void)` | test seam |
| `tfw_alias_parts_t` | `{nonce, ueid, fw_hash, pubkey, sig}` — lets the DER vectors be pinned with no crypto provider linked at all |
| `tfw_alias_tbs_digest(const tfw_alias_parts_t *, uint8_t out[32])` | SHA-256 over the TBSCertificate |
| `tfw_alias_cert(const tfw_alias_parts_t *, uint16_t off, uint8_t *out, uint8_t cap, uint16_t *total, uint8_t *got)` | emits one window of the certificate |
| `tfw_alias_cmd` / `tfw_alias_reset_state` | dispatch handler for `ATTEST_CERT`; test seam |
| `tfw_alias_sources(const tfw_alias_parts_t *, const uint8_t *own, uint8_t uccs[151], const uint8_t *src[])` | encodes the claims set and fills the source slots (`TFW_ALIAS_SRC_*`) both templates' steps read from |
| `tfw_csr_cri_digest(const tfw_alias_parts_t *, uint8_t out[32])` | SHA-256 over the CertificationRequestInfo; `p->nonce` is `b`, `p->sig` unread |
| `tfw_csr_cri(const tfw_alias_parts_t *, uint16_t off, uint8_t *out, uint8_t cap, uint16_t *total, uint8_t *got)` | emits one window of the CRI, with `tfw_alias_cert`'s contract; the command never sends it, so this is the seam the vectors pin |
| `tfw_csr_cmd(tfw_reader_t *, tfw_writer_t *)` | dispatch handler for `ATTEST_CSR` |
| `tfw_tmpl_step_t` / `tfw_tmpl_emit` / `tfw_tmpl_digest` | the one template walker: a PROGMEM table of `{src, off, n}` steps whose kind is a verbatim PROGMEM run, a RAM run, a RAM run as lowercase hex, or a positive-INTEGER byte; `emit` copies a window out, `digest` hashes the whole |
| `tfw_uccs_encode(tfw_writer_t *, const uint8_t *nonce, uint8_t nonce_len, const uint8_t *ueid, const uint8_t *fw_hash)` | the claims set only; not a general CBOR encoder and must not become one |

### `include/tfw/protocol.h` — wire constants

`TFW_CMD_ATTEST` 0x60, `TFW_CMD_ATTEST_CERT` 0x63, `TFW_CMD_ATTEST_CSR` 0x64 · `TFW_ATTEST_VERSION` 1,
`TFW_ATTEST_NONCE_LEN` 16, `TFW_ATTEST_ID_LEN` 16, `TFW_ATTEST_LEN` 164,
`TFW_ATTEST_PAGE` 48 · `TFW_ALIAS_NONCE_LEN` 32, `TFW_ALIAS_PAGE` 60 ·
`TFW_ALG_NONE` 0x00, `TFW_ALG_ECDSA_P256_SHA256` 0x01, `TFW_ALG_ED25519` 0x02,
`TFW_ALG_HMAC_SHA256` 0x10 · `TFW_UCCS_LEN` 151, `TFW_UCCS_COSWID_LEN` 84
· `TFW_UCCS_OFF_UEID` 40, `TFW_UCCS_UEID_LEN` 17 (`src/caps/uccs.h`) ·
`TFW_CSR_CRI_LEN` 365 (`src/caps/csr.h`).

### `sdk/go` (package `tfw`)

`Device.Attest(ctx, nonce []byte) ([]byte, error)`,
`Device.AttestCert(ctx, nonce []byte) ([]byte, error)` — `sdk/go/attest.go`.
`CmdAttest`, `CmdAttestCert`, `AttestNonceLen`=16, `AliasNonceLen`=32,
`EvidenceLen`=164. `CryptoTier` (`CryptoNone`/`CryptoSoftware`/
`CryptoSecureElement`/`CryptoTPM2`), `Caps.CryptoTier`, `Caps.Algorithms`,
`FeatureAttest` — `sdk/go/device.go`.

`Device.AttestCSR(ctx, b [AliasNonceLen]byte) ([CSRSigLen]byte, error)`,
`CmdAttestCSR` 0x64, `CSRSigLen` (64), `FeatureAttestCSR` (CAPS bit 15) —
`sdk/go/attestcsr.go`. Transport only: it checks CAPS bit 15, sends
`ATTEST_CSR`, and returns the device's `r‖s`. The CertificationRequestInfo, the
DER wrapping and the check of the signature are certificate-profile work and
go-xkms's (go-xkms#1441), so `sdk/go` holds no copy of the template and no Go
mirror of the CSR vectors; `TFWV_CSR_CRI` and `TFWV_CSR_CRI_DIGEST` stay in
`testdata/vectors.h`, pinned by `test_csr.c`.

## Invariants

- **The nonce is always inside the signed span.** `attest.c` hashes
  `evidence[0:OFF_ALG)` (74 bytes) before signing, which includes
  `OFF_NONCE`; `alias.c` signs a digest of the whole TBS, which includes the
  UCCS claims set. An evidence frame or certificate whose nonce falls outside
  what was hashed is not evidence of anything.
- **A page fetch with nothing minted is refused**, not zero-filled:
  `tfw_attest_cmd`/`tfw_alias_cmd` return `TFW_ERR_BAD_ARGC` when `have_evidence`
  / `have_cert` is false. A stale read must not look like a valid answer.
- **Page offsets are computed at 16-bit width before narrowing.** `page *
  TFW_ATTEST_PAGE` overflows a `uint8_t` at page 6; the bound is checked wide
  or it is not a bound (`src/caps/attest.c`). `TFW_ALIAS_PAGE` (60) times a
  byte page stays inside `uint16_t`, so `tfw_alias_cert`'s own bound suffices.
- **A page at or past the end is `TFW_ERR_RANGE`, never an empty success.** An
  empty success is indistinguishable from "the document ended here" to a
  host that has lost count.
- **The TBS template is size-asserted, not trusted.** `_Static_assert
  (TBS_PREFIX_LEN == 232)`, `_Static_assert(TBS_LEN == 383)` in
  `src/caps/alias.c`. `TFW_UCCS_LEN` (151) is asserted equal to the envelope
  (67) plus the CoSWID (84) in `src/caps/uccs.c`. A claims set of any other
  length would produce DER length fields that disagree with the bytes that
  follow them.
- **`tfw_uccs_encode` refuses a nonce of any width but `TFW_ALIAS_NONCE_LEN`**
  (`TFW_ERR_RANGE`) and refuses a claims set that did not fit exactly
  (`TFW_ERR_TOO_LONG`); nothing partial is ever reported as success.
- **A certificate is refused rather than mis-labelled.** `alias.c`'s `mint()`
  requires the provider to report `TFW_ALG_ECDSA_P256_SHA256`; any other
  algorithm returns `TFW_ERR_UNSUPPORTED` rather than a certificate whose
  `signatureAlgorithm` field lies about what signed it. `tfw_csr_cmd` refuses
  the same way, for the CSR the host will wrap around its signature.
- **The DeviceID key signs only fixed templates whose host fields are
  fixed-width.** Three structures, no more: the `ATTEST` frame, the alias
  `TBSCertificate`, and the `ATTEST_CSR` CertificationRequestInfo. The host
  contributes a nonce to each — 16 or 32 bytes, checked by the dispatcher — and
  nothing else; no host value can change a length, a tag or which document it
  is. A fourth signed structure, or a host-supplied digest, subject or
  extension, is a change to `docs/security/threat-model.md` first.
- **Each signed structure is unparseable as the others.** The frame opens
  `0x01`, not a SEQUENCE; the TBS has `A0 03` (its `[0]` version) at offset 4
  where the CRI has `02 01 00`; the CRI's `30 00` at offset 7 is where a TBS
  needs an AlgorithmIdentifier with a mandatory OID. `test/unit/test_csr.c`
  checks all six directions against real output.
- **One walker, one copy of each shared header.** The CRI reads its
  SubjectPublicKeyInfo header and its evidence-Extension header out of the
  alias `TBS_PREFIX` (`TFW_ALIAS_TBS_OFF_SPKI_HDR`, `TFW_ALIAS_TBS_OFF_UCCS_EXT`),
  and both documents are emitted by `tfw_tmpl_emit`. The CRI's permanentIdentifier
  is the lowercase hex of the full UEID (type byte + DeviceID, 34 digits,
  go-xkms#1441), read from the `ueid` claim inside the encoded claims set at
  `TFW_UCCS_OFF_UEID`, so there is one source for the UEID, not two. Every DER
  length above that hole is derived at compile time from the hole widths;
  `csr.c` asserts that its steps add up to 365 and that its outer length is
  365 − 4.
- **The two nonces never merge.** `TFW_ATTEST_NONCE_LEN` (16, runtime
  freshness) and `TFW_ALIAS_NONCE_LEN` (32, enrolment) are checked by the
  dispatcher's declared arity in `src/core/commands.def`; a command sees only
  its own width.
- **CDI zeroization is tier-specific, and both tiers are honest about it
  (issue #7).** The element tier (`src/crypto/atecc608a/provider.c`) never
  signs with the CDI — the DeviceID key is element-resident, not
  CDI-derived — so `tfw_crypto_init` holds it in a stack buffer that exists
  only long enough to learn whether the element has a UDS, and clears it
  before returning: the normative rule (DICE HW Requirements §6.4, "cleared
  once no longer needed") is genuinely met there. The software tier
  (`src/crypto/soft/provider.c`) cannot meet it: its CDI keys
  `tfw_crypto_hmac`, is the signing key of the HMAC identity (`id_hmac.c`,
  the ATmega328P and ATmega32U4) and gates every signature of the P-256 one
  (`id_p256.c`, whose seed and scalar are derived from the UDS per call and
  zeroed), so it stays resident for the process lifetime by necessity, and
  the code says so rather than claiming otherwise.

## Error semantics

| Status | Where it comes from here | Meaning |
|---|---|---|
| `TFW_ERR_BAD_ARGC` | dispatcher arity check; page fetch with no prior mint; `ATTEST_CSR` with any `b` but 32 bytes | payload length wrong, or a page asked for before a nonce |
| `TFW_ERR_RANGE` | page past the end; `tfw_uccs_encode`'s nonce-width check | value outside the permitted interval |
| `TFW_ERR_NOT_PROVISIONED` | `tfw_crypto_sign`/`pubkey` on a present-but-unseeded element | identity has not been provisioned yet |
| `TFW_ERR_CRYPTO` | any provider op on `TFW_CRYPTO_NONE`, or an element that will not wake | the crypto provider failed the operation |
| `TFW_ERR_UNSUPPORTED` | `ATTEST_CERT`/`ATTEST_CSR` compiled in under a provider with no asymmetric key (a host test build; no shipped soft or none image carries them); an algorithm mismatch in `mint()` or `tfw_csr_cmd` | this root of trust cannot do it |
| `TFW_ERR_UNKNOWN_CMD` | `ATTEST`/`ATTEST_CERT`/`ATTEST_CSR` on a build with the feature off | no table entry for this command id |
| `TFW_ERR_INTERNAL` | `tfw_alias_sources` if the claims set did not encode to `TFW_UCCS_LEN` | a condition the firmware believes impossible |
| `TFW_ERR_TOO_LONG` | `tfw_uccs_encode` overflow | request/response exceeded a bound |

`TFW_ERR_UNSUPPORTED` vs `TFW_ERR_UNKNOWN_CMD` is deliberate: the first says
this firmware knows what the command is and this root of trust cannot answer
it; the second says the command does not exist in this build at all.

## Configuration

| Symbol | Default | Effect |
|---|---|---|
| `TFW_FEATURE_ATTEST` | 0 | gates `ATTEST` into `src/core/commands.def`; "lands with the crypto provider" |
| `TFW_FEATURE_ALIAS_CERT` | 0 | gates `ATTEST_CERT`; on only where the linked provider has an asymmetric key |
| `TFW_FEATURE_ATTEST_CSR` | 0 | gates `ATTEST_CSR` and `src/caps/csr.c`; selected only beside `ALIAS_CERT`, and a board profile can turn it off alone. CAPS bit 15 |
| `TFW_FEATURE_PROVISION` | 0 | gates `PROV_GENKEY`/`PROV_SEED_UDS`/`PROV_LOCK`/etc.; a jig-only image |
| `CONFIG_DICE_STRICT_DEVICEID` | off | A2: DeviceID derived from the CDI at provisioning instead of random (A1, default) — see `docs/device-provisioning/user.md` |
| `CRYPTO=` | `soft` (AVR) | link-time provider selection: `soft`, `atecc608a`, or `none` (`mk/firmware.mk`) |

`TFW_FEATURE_ALIAS_CERT` is off on `boards/attiny85` and
`boards/atmega32u4` (`board_profile.h` in each) — the ATtiny85 has no
asymmetric tier at all, and the ATmega32U4's DFU-bootloader flash budget does
not fit the certificate builder alongside everything else. `ATTEST` still
works on both; only `ATTEST_CERT` is absent.

`TFW_FEATURE_ATTEST_CSR` follows `ALIAS_CERT` and so is absent on both too. Two
profiles also turn it off by name, because a part is excluded (#6) from ACME enrolment if the command puts it over budget:

- **ATmega32U4** — so it stays off even if the certificate builder ever fits.
- **ATmega328P** — its element image with the command measured 16 884 / 16 384
  bytes, 500 over (CI on ac493c8). The alias certificate stays on there; only
  the possession signature is out.

So the **ATmega2560 element tier is the only image that carries `ATTEST_CSR`**,
and the only part that can prove possession of its DeviceID key at ACME
finalize. Everywhere else 0x64 answers `TFW_ERR_UNKNOWN_CMD` and CAPS bit 15 is
clear. `make board-sweep` compiles each board's descriptor test with its
element tier's own feature flags (`make feature-defs`) and pins this per board;
`test_dispatch` pins the unknown-command answer on the 328P build.

## Test strategy

| What | Where |
|---|---|
| Frame/claims/certificate unit tests | `test/unit/test_attest.c`, `test/unit/test_alias.c`, `test/unit/test_csr.c`, `test/unit/test_uccs.c` — run with `make test` |
| CSR vectors | `TFWV_CSR_CRI` (365 bytes) and `TFWV_CSR_CRI_DIGEST` in `testdata/vectors.h`, from an independent encoder and checked with `openssl req`; `test_csr.c` also checks that the alias certificate is byte-identical, that `b` moves exactly 32 bytes, and that the signature verifies under the model key |
| Wire vectors, pinned literals | `testdata/vectors.h`, `testdata/uccs_vectors.h`; a vector mismatch is a wire break, not a test to update |
| Malformed-certificate vectors | `TFWV_ALIAS_CERT_BAD_OUTER` (every X.509 parser rejects it), `TFWV_ALIAS_CERT_BAD_INNER` (OpenSSL and `cryptography` both accept it — the extension-blind failure this project has to fear; `test_alias.c` walks the extension's own DER to catch it) |
| Go SDK paging and nonce-width tests | `sdk/go/attest_test.go` — `make sdk-test` |
| Cross-implementation / simulator | `sdk/go/cmd/tfwctl/e2e_firmware_test.go`, against `tfwsim` (`make tfwsim`, then `make ci`) |
| Element-tier simulator: evidence over the wire | `sdk/go/tfwsim/dice_test.go` and `launch_test.go`, against `tfwsim-atecc608a` (`make tfwsim-atecc608a`; `make sdk-test` builds it). The alias certificate parses with `crypto/x509`, carries ueid and FWHash and binds the nonce; its signature verifies under the DeviceID test key with `x509.CheckSignature` and `ecdsa.VerifyASN1`, and so does the `ATTEST` frame's with `ecdsa.Verify`, and each rejects a flipped bit or another key. `auth_test.go` authenticates with an operator key generated in Go. The C side pins the model's curve against RFC 6979 A.2.5 and an independent derivation (`test/unit/test_model_p256.c`). |
| Element-backed build | `make firmware-attested` (`CRYPTO=atecc608a`); the default `make firmware` never exercises the element path |
| C/Go vector agreement | `make verify-vectors` |

## Observability

There is no logging subsystem on the device; the only signal a host has is
`CAPS`. The crypto tier (byte 9) and the algorithm bitmap (bytes 14–15, both
little-endian within the `CAPS` payload) are reported **before** a host
decides what any evidence is worth — `src/caps/sys.c`. `tfwctl caps` surfaces
both as `root of trust` and `algorithms`; see `cli.md`.
