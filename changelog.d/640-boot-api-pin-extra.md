### Fixed

- Boot API page drift (#640): `docs/secure-boot/api.md` now states what the `pubkey_hash` pin hashes on each P-256 and Ed25519 build and that the mps2-an500 and mps2-an505 A/B builds keep the swap journal and confirm request in `[0x0C0, 0x140)`; the ATmega328P page cites `mk/boot.mk:442` for its `BOOT_SUPPORTED` row.
