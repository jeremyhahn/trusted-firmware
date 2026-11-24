### Changed

- The ATmega328P's flash budget is its 28 672-byte application region (owner ruling 1243, option (a) of the Ethernet ADR), and its element image carries ATTEST_CSR, so a Nano can prove possession of its DeviceID key at ACME finalize; CAPS reports bit 15. The element ETH=1 image is not yet a row: its last measured RAM is 1 121 of 1 024 bytes.
