### Added

- **The OPTIGA Trust M command layer signs, and a crypto provider sits on
  it** (row `optiga-trust-m`). CalcSign (Solution Reference Manual 3.70,
  §4.4.1.12, Table 51) runs ECDSA P-256 over a SHA-256 digest with the factory
  key `0xE0F0`. Its DER r and s come back as a raw 64-byte r‖s, and any other
  shape is refused with `TFW_ERR_FRAMING`.
  `src/crypto/optiga/optiga_crypto.c` maps the crypto interface:
  - random is GetRandom.
  - sign is CalcSign.
  - counter is GetDataObject on `0xE120`-`0xE123`.
  - SHA-256 is software.
  - HMAC, pubkey and both verify calls return `TFW_ERR_UNSUPPORTED`.

  There is no key, no secret and no write path. No firmware tier links the
  provider yet, so no board's image changes. No wire vector, command id or
  status code changes.
