### Fixed

- **`docs/dice-attestation/architecture.md` sizes the alias certificate as the code does** (#629): the alias `TBSCertificate` is 400 bytes on a 249-byte template (401 under the software root). The none tier answers `TFW_ERR_UNSUPPORTED`, with `TFW_ERR_CRYPTO` from random only. `CRYPTO=soft` is the fallback, not the AVR boards' default. The SHA-256 tests are named correctly, and every `path:line` reference points at the code on main.
