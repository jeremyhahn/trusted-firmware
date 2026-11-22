### Fixed

- **The Ethernet link's `src/net` comments match the code** (#629): only the W5100 file masks buffer offsets, the 4 MHz clock is w5x00.c's and the poll bound covers all four waits, `w5x_chip_init` and `on_reply` name every failure result, and the DHCP header names the offer checks, the DECLINE flag, the renew clamp, the BOUND deadline and the short-header 0xFF.
