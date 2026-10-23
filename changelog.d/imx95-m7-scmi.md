### Added

- **An SCMI client for the i.MX 95 M7, tested on the host** (SPEC row `imx95`).
  `src/hal/cortex-m/imx95/scmi.c` encodes and checks Arm DEN0056 messages:
  the header, the Base protocol's PROTOCOL_VERSION and PROTOCOL_ATTRIBUTES,
  the Clock protocol's CLOCK_CONFIG_SET and CLOCK_RATE_GET, the Power domain
  protocol's POWER_STATE_SET and the Reset domain protocol's RESET, and every
  status code. `src/hal/cortex-m/imx95/scmi_mu.c` carries them over a shared
  memory channel rung through an MU doorbell. It polls, with a bounded wait.
  `test/unit/test_imx95_scmi.c` pins each command's bytes as literals.
  `test/unit/test_imx95_scmi_mu.c` runs the channel against the register
  model's MU and shared memory: a wrong token or protocol is refused, a busy
  channel times out, and an oversized message is refused. No document held
  here gives the M7 agent's MU, its shared memory or the LPUART3 clock id
  (imx95-design.md question 21), so nothing calls the client yet.

### Changed

- **The SCMI client cites DEN0056E by section and table.** Every value was
  checked against DEN0056E (version 3.2, issue E), and the README's entry no
  longer marks the sections TO CONFIRM. CLOCK_CONFIG_SET's third word is
  renamed `extended_config_val`, the name §4.6.2.9 gives it; its bytes are
  unchanged. The MU's GCR offset, `0x114`, stays unconfirmed against IMX95RM.
  Linux's `imx-mailbox.c` agrees with it, as a secondary source.
- **`docs/reference/` lists the sources behind the M7's MUs.** These are
  RM00284 Rev. 4.5 (the enclave library's API, which holds no wire format),
  imx-secure-enclave d7e1180b, U-Boot ece349ad, and Linux v6.12
  `imx-mailbox.c`. Each is listed with its revision and URL, and none is
  copied.
