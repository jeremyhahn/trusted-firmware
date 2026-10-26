### Added

- **The `tfwctl` wire suite runs on both QEMU Cortex-M boards** (row
  `cortex-m`, #161, #163). `make integration-test-cortex-m-wire` boots the
  signed `mps2-an505` and `mps2-an500` images with their UART on a pty and runs
  `hw-test` against it; `sdk/go/internal/peer` resets the guest with QMP
  `system_reset` when `TFW_HW_RESET=qmp:<socket>` is set (`make hw-test
  HW_RESET=`), and by DTR otherwise. `peerBoards` has rows for both boards, and
  a row whose family CAPS does not report checks that the board refuses the
  command. CI runs it in the toolchain image after `integration-test-cortex-m`.
