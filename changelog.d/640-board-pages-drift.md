### Fixed

- **Eight board-page claims put right against the ports and build files**
  (#640). The FRDM-i.MX95 M7 page now says the SCMI client is called by
  `hsm_port.c`, PWM waits on the pad handoff, the enclave tier's message layer
  is built and host-tested, and the HSM channel has `0x7C` and an M7 port;
  the RP2350 page lists `cortex-m` as in progress and I²C standard mode; the
  board-id registry names `include/tfw/config.h`.
