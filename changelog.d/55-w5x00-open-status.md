### Added

- **A W5x00 OPEN the socket status does not confirm has a test** (#55).
  `test_an_open_the_status_does_not_confirm_is_closed_and_refused` in
  `test/unit/test_w5x00_mutate.c` answers Sn_SR with SOCK_CLOSED or SOCK_UDP
  after OPEN and pins `src/net/w5x00.c:133`'s refusal: `tfw_eth_start()`
  returns `TFW_ERR_INTERNAL`, the socket is closed, and no peer address or
  port is written, on both the W5100 and the W5500.
