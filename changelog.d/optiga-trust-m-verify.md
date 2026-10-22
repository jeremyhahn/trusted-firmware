### Added

- **The OPTIGA Trust M tier verifies the operator's signature** (row
  `optiga-trust-m`). VerifySign (Solution Reference Manual 3.70, §4.4.1.13,
  Table 52) checks a raw r‖s ECDSA P-256 signature over a SHA-256 digest
  against the operator's public key, sent in the command. The signature is
  encoded as two minimal DER INTEGERs, the key as a DER BIT STRING.
  `tfw_crypto_verify_operator` in `src/crypto/optiga/optiga_crypto.c` calls
  it. A signature that does not verify is `TFW_ERR_CRYPTO`, and a transport
  failure is returned as it came.

  No board links the provider yet, so no board's image changes. No wire
  vector, command id or status code changes.
