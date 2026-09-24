# DICE attestation — engineering

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

See `architecture.md` for design and boundaries; not restated here.

## Exported surface

### `include/ag/crypto.h` — the provider contract, substituted at link time

| Symbol | Signature | Note |
|---|---|---|
| `ag_crypto_init` | `ag_status_t(void)` | derives the CDI if a UDS is present; never errors on an unprovisioned device |
| `ag_crypto_tier` | `ag_crypto_tier_t(void)` | `AG_CRYPTO_NONE`\|`SOFTWARE`\|`SECURE_ELEMENT`\|`TPM2` = 0/1/2/3 |
| `ag_crypto_algorithms` | `uint16_t(void)` | bitmap: `AG_ALGBIT_ECDSA_P256_SHA256`\|`ED25519`\|`HMAC_SHA256` |
| `ag_crypto_random` | `ag_status_t(uint8_t *out, uint8_t len)` | `AG_ERR_CRYPTO` on a tier with no entropy source |
| `ag_crypto_sha256` | `ag_status_t(const uint8_t *data, uint16_t len, uint8_t out[32])` | |
| `ag_crypto_hmac` | `ag_status_t(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32])` | the DICE one-way function; `slot` never crosses the bus |
| `ag_crypto_sign` | `ag_status_t(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id)` | writes the algorithm actually used |
| `ag_crypto_pubkey` | `ag_status_t(uint8_t out[64])` | `AG_ERR_NOT_PROVISIONED` or `AG_ERR_UNSUPPORTED` (symmetric tier) |
| `ag_crypto_counter` | `ag_status_t(uint8_t idx, uint32_t *out)` | monotonic; backs `BootCount` and rollback |

### `src/caps/attest.h` / `src/caps/alias.h` / `src/caps/uccs.h`

| Symbol | Note |
|---|---|
| `ag_attest_cmd(ag_reader_t *, ag_writer_t *)` | dispatch handler for `ATTEST` |
| `ag_attest_device_id()` / `ag_attest_fw_hash()` | pointers into the evidence buffer, `AG_ATTEST_ID_LEN`/`AG_SHA256_LEN` bytes; zero until the bootloader handoff lands (`architecture.md`) |
| `ag_attest_reset_state(void)` | test seam |
| `ag_alias_parts_t` | `{nonce, ueid, fw_hash, pubkey, sig}` — lets the DER vectors be pinned with no crypto provider linked at all |
| `ag_alias_tbs_digest(const ag_alias_parts_t *, uint8_t out[32])` | SHA-256 over the TBSCertificate |
| `ag_alias_cert(const ag_alias_parts_t *, uint16_t off, uint8_t *out, uint8_t cap, uint16_t *total, uint8_t *got)` | emits one window of the certificate |
| `ag_alias_cmd` / `ag_alias_reset_state` | dispatch handler for `ATTEST_CERT`; test seam |
| `ag_uccs_encode(ag_writer_t *, const uint8_t *nonce, uint8_t nonce_len, const uint8_t *ueid, const uint8_t *fw_hash)` | the claims set only; not a general CBOR encoder and must not become one |

### `include/ag/protocol.h` — wire constants

`AG_CMD_ATTEST` 0x60, `AG_CMD_ATTEST_CERT` 0x63 · `AG_ATTEST_VERSION` 1,
`AG_ATTEST_NONCE_LEN` 16, `AG_ATTEST_ID_LEN` 16, `AG_ATTEST_LEN` 164,
`AG_ATTEST_PAGE` 48 · `AG_ALIAS_NONCE_LEN` 32, `AG_ALIAS_PAGE` 60 ·
`AG_ALG_NONE` 0x00, `AG_ALG_ECDSA_P256_SHA256` 0x01, `AG_ALG_ED25519` 0x02,
`AG_ALG_HMAC_SHA256` 0x10 · `AG_UCCS_LEN` 152, `AG_UCCS_COSWID_LEN` 85
(`src/caps/uccs.h`).

### `sdk/go` (package `aggeneric`)

`Device.Attest(ctx, nonce []byte) ([]byte, error)`,
`Device.AttestCert(ctx, nonce []byte) ([]byte, error)` — `sdk/go/attest.go`.
`CmdAttest`, `CmdAttestCert`, `AttestNonceLen`=16, `AliasNonceLen`=32,
`EvidenceLen`=164. `CryptoTier` (`CryptoNone`/`CryptoSoftware`/
`CryptoSecureElement`/`CryptoTPM2`), `Caps.CryptoTier`, `Caps.Algorithms`,
`FeatureAttest` — `sdk/go/device.go`.

## Invariants

- **The nonce is always inside the signed span.** `attest.c` hashes
  `evidence[0:OFF_ALG)` (74 bytes) before signing, which includes
  `OFF_NONCE`; `alias.c` signs a digest of the whole TBS, which includes the
  UCCS claims set. An evidence frame or certificate whose nonce falls outside
  what was hashed is not evidence of anything.
- **A page fetch with nothing minted is refused**, not zero-filled:
  `ag_attest_cmd`/`ag_alias_cmd` return `AG_ERR_BAD_ARGC` when `have_evidence`
  / `have_cert` is false. A stale read must not look like a valid answer.
- **Page offsets are computed at 16-bit width before narrowing.** `page *
  AG_ATTEST_PAGE` overflows a `uint8_t` at page 6; the bound is checked wide
  or it is not a bound (`src/caps/attest.c`). `AG_ALIAS_PAGE` (60) times a
  byte page stays inside `uint16_t`, so `ag_alias_cert`'s own bound suffices.
- **A page at or past the end is `AG_ERR_RANGE`, never an empty success.** An
  empty success is indistinguishable from "the document ended here" to a
  host that has lost count.
- **The TBS template is size-asserted, not trusted.** `_Static_assert
  (TBS_PREFIX_LEN == 230)`, `_Static_assert(TBS_LEN == 382)` in
  `src/caps/alias.c`. `AG_UCCS_LEN` (152) is asserted equal to the envelope
  (67) plus the CoSWID (85) in `src/caps/uccs.c`. A claims set of any other
  length would produce DER length fields that disagree with the bytes that
  follow them.
- **`ag_uccs_encode` refuses a nonce of any width but `AG_ALIAS_NONCE_LEN`**
  (`AG_ERR_RANGE`) and refuses a claims set that did not fit exactly
  (`AG_ERR_TOO_LONG`); nothing partial is ever reported as success.
- **A certificate is refused rather than mis-labelled.** `alias.c`'s `mint()`
  requires the provider to report `AG_ALG_ECDSA_P256_SHA256`; any other
  algorithm returns `AG_ERR_UNSUPPORTED` rather than a certificate whose
  `signatureAlgorithm` field lies about what signed it.
- **The two nonces never merge.** `AG_ATTEST_NONCE_LEN` (16, runtime
  freshness) and `AG_ALIAS_NONCE_LEN` (32, enrolment) are checked by the
  dispatcher's declared arity in `src/core/commands.def`; a command sees only
  its own width.
- **Known gap: the CDI is not zeroized after use.** Both `src/crypto/soft/
  provider.c` and `src/crypto/atecc608a/provider.c` keep a static `cdi[32]`
  resident for the process lifetime; neither ever clears it. The normative
  rule is that it must be cleared once no longer needed (DICE HW
  Requirements §6.4) — this is a defect against that rule, tracked as
  code, not documented as met.

## Error semantics

| Status | Where it comes from here | Meaning |
|---|---|---|
| `AG_ERR_BAD_ARGC` | dispatcher arity check; page fetch with no prior mint | payload length wrong, or a page asked for before a nonce |
| `AG_ERR_RANGE` | page past the end; `ag_uccs_encode`'s nonce-width check | value outside the permitted interval |
| `AG_ERR_NOT_PROVISIONED` | `ag_crypto_sign`/`pubkey` on a present-but-unseeded element | identity has not been provisioned yet |
| `AG_ERR_CRYPTO` | any provider op on `AG_CRYPTO_NONE`, or an element that will not wake | the crypto provider failed the operation |
| `AG_ERR_UNSUPPORTED` | `ATTEST_CERT` on the software or none tier; an algorithm mismatch in `mint()` | compiled out, or this root of trust cannot do it |
| `AG_ERR_UNKNOWN_CMD` | `ATTEST`/`ATTEST_CERT` on a build with the feature off | no table entry for this command id |
| `AG_ERR_INTERNAL` | `emit_tbs` if the claims set did not encode to `AG_UCCS_LEN` | a condition the firmware believes impossible |
| `AG_ERR_TOO_LONG` | `ag_uccs_encode` overflow | request/response exceeded a bound |

`AG_ERR_UNSUPPORTED` vs `AG_ERR_UNKNOWN_CMD` is deliberate: the first says
this firmware knows what the command is and this root of trust cannot answer
it; the second says the command does not exist in this build at all.

## Configuration

| Symbol | Default | Effect |
|---|---|---|
| `AG_FEATURE_ATTEST` | 0 | gates `ATTEST` into `src/core/commands.def`; "lands with the crypto provider" |
| `AG_FEATURE_ALIAS_CERT` | 0 | gates `ATTEST_CERT`; on only where the linked provider has an asymmetric key |
| `AG_FEATURE_PROVISION` | 0 | gates `PROV_GENKEY`/`PROV_SEED_UDS`/`PROV_LOCK`/etc.; a jig-only image |
| `CONFIG_DICE_STRICT_DEVICEID` | off | A2: DeviceID derived from the CDI at provisioning instead of random (A1, default) — see `docs/device-provisioning/user.md` |
| `CRYPTO=` | `soft` (AVR) | link-time provider selection: `soft`, `atecc608a`, or `none` (`mk/avr.mk`) |

`AG_FEATURE_ALIAS_CERT` is off on `boards/attiny85` and
`boards/atmega32u4` (`board_profile.h` in each) — the ATtiny85 has no
asymmetric tier at all, and the ATmega32U4's DFU-bootloader flash budget does
not fit the certificate builder alongside everything else. `ATTEST` still
works on both; only `ATTEST_CERT` is absent.

## Test strategy

| What | Where |
|---|---|
| Frame/claims/certificate unit tests | `test/unit/test_attest.c`, `test/unit/test_alias.c`, `test/unit/test_uccs.c` — run with `make test` |
| Wire vectors, pinned literals | `testdata/vectors.h`, `testdata/uccs_vectors.h`; a vector mismatch is a wire break, not a test to update |
| Malformed-certificate vectors | `AGV_ALIAS_CERT_BAD_OUTER` (every X.509 parser rejects it), `AGV_ALIAS_CERT_BAD_INNER` (OpenSSL and `cryptography` both accept it — the extension-blind failure this project has to fear; `test_alias.c` walks the extension's own DER to catch it) |
| Go SDK paging and nonce-width tests | `sdk/go/attest_test.go` — `make sdk-test` |
| Cross-implementation / simulator | `sdk/go/agsim_test.go`, against `agsim` (`make agsim`, then `make ci`) |
| Element-backed build | `make firmware-attested` (`CRYPTO=atecc608a`); the default `make firmware` never exercises the element path |
| C/Go vector agreement | `make verify-vectors` |

## Observability

There is no logging subsystem on the device; the only signal a host has is
`CAPS`. The crypto tier (byte 9) and the algorithm bitmap (bytes 14–15, both
little-endian within the `CAPS` payload) are reported **before** a host
decides what any evidence is worth — `src/caps/sys.c`. `agctl caps` surfaces
both as `root of trust` and `algorithms`; see `cli.md`.
