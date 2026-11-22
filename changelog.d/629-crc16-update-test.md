### Added

- **`sdk/go/crc_update_test.go`, a test of the exported `tfw.CRC16Update`** (#629): it honours any seed it is given (the CRC-16/XMODEM, CRC-16/SPI-FUJITSU and our 0xFFFF check values over "123456789", and one byte 0x01 from zero giving the polynomial 0x1021), and folding a buffer's tail onto CRC16 of its head gives CRC16 of the whole, at every split of the check input and at the middle of every frame vector.
