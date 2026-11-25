### Added

- **Tests of the edges of the address `tfwsim.StartElementEthernet` accepts from `tfwsim --netcfg-addr`** (#629): `TestStartElementEthernetAcceptsTheLastBlockTheEEPROMHolds` writes the network block at 0x0FE0, the last 32 bytes of the 4 KiB EEPROM, and `TestStartElementEthernetRefusesAnAddressOneByteOutside` refuses 0x00BF (the platform block's last byte) and 0x0FE1 (one byte past the EEPROM) before a simulator starts.
