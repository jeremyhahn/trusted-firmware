### Fixed
- tfw-boot's nRF54L UART waits read TXDRDY and RXTO on every pass, and the host tests prove a send and an off wait for an event that comes late (#518)
