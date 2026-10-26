### Documentation

- **Why the `tfwctl` wire suite does not yet run on the QEMU boards** (row
  `cortex-m`). `docs/cortex-m/README.md` records that `sdk/go/internal/peer`
  resets its device by DTR and waits for it to start, which a QEMU pty cannot
  do, and sets out the reset seam (QMP `system_reset`) and `peerBoards` rows
  that `integration-test-cortex-m-wire` needs first.
