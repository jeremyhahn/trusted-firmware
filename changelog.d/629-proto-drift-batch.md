### Fixed

- **The wire-layer code comments match the build** (#629): crc16.h cites
  mk/firmware.mk's FW_SRCS and the 71-byte frame, frame.h names the real
  framer fuzz test and the encoder's C-only callers, link.h names the
  STM32H5's hal_eth_wire.c, link_eth.c's allow-list note includes the OTA
  staging four, TFW_EVENT_DEADMAN and TFW_EVENT_COUNTER_WRAP are marked
  reserved, not emitted, and transport.c's BAD_FORMAT note points above.
