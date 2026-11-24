### Added

- **The FRDM-MCXN947's hal_soc.c reset cause has a host test** (#628): it reads CMC0's SRS at the manual's address, writes nothing, and reports a software reset only behind the firmware's own RESETREQ marker.
