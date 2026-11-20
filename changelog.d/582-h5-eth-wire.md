### Added

- STM32H573I-DK: tfw-wire over TCP on the SoC's own Ethernet MAC (#582). The
  `ETH_MAC=1` image's link is `src/hal/cortex-m/stm32h5/hal_eth_wire.c`, which
  serves the UART and one TCP connection on port 21574 (the ESP32 port's wire
  port) to the one transport; the lwIP raw-API listener is
  `hal_eth_wire_tcp.c`. Host-tested by `test/unit/test_h5_eth_wire.c`.
  Verified on the board 2026-11-17: the P4's tfw-wire row set runs over
  `make hw-test BOARD=stm32h573i-dk PORT=tcp:<board>:21574` with PR #568's
  host transport; all pass but the boot-event row, which a TCP connect cannot
  serve (as on the P4). `ETH_MAC=1` with `ETH=1` is refused.
