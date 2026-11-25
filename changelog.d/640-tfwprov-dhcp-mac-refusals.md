### Fixed

- **A test pins tfwprov's `--dhcp` netblock refusals** (#640): `TestNetConfigDHCPRefusesUnparsableFlags` requires the `--dhcp` path to refuse an unparsable `--mac`, an 8-byte EUI-64 `--mac`, and an `--endpoint` with no port or no address; before it, deleting any of those guards passed every test. Test only.
