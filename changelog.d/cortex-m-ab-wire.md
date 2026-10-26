### Added

- **The QEMU wire run on the MPS2 A/B build** (#161). On both MPS2 boards,
  `make integration-test-cortex-m-wire` now also runs `cortex-m-qemu-ab-wire`.
  It boots the A/B stager under the `…:0:1:ab` tfw-boot, from the factory
  copies, and runs four tests from `sdk/go/cmd/tfwctl/ota_ab_qemu_test.go`
  (`qemu` tag), each on a QEMU of its own:
  - OTA_INFO on both slots;
  - a confirmed update kept across two resets, with the floor at 2;
  - an unconfirmed one rolled back, with the fell-back flag;
  - an image below the floor that tfw-boot refuses, while the confirmed one
    runs on.

  `cortex-m-qemu-cycle` takes the test to run as `CYCLE_TEST`. `cm_image`
  also signs `staged.bin` and `below-floor.bin` for the A/B row. No wire
  vector, status code, command id or board id changed.

### Fixed

- **The MPS2 A/B build's trial deadline.** The A/B application links
  `src/core/ota_trial.c`, but neither MPS2 profile defined
  `TFW_OTA_TRIAL_TIMEOUT_S`, so the build could not compile. Both now set
  60 s, as every board with a trial does.
