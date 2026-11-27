### Fixed

- **The STM32H5 Ethernet link's header no longer says every command is admitted
  on either wire** (#599). It now says what `tfw_link_admits()` does: a TCP peer
  is admitted only the Ethernet allow-list, and the UART carries every command,
  so device control and provisioning stay on serial. A transport-level test
  shows a RESET refused over TCP is still admitted and carried out over the UART.
