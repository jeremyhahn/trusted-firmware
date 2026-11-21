### Fixed

- **docs/device-capabilities/cli.md and user.md re-verified against `tfwctl`**
  (#627). cli.md names the files each verb is dispatched in, the serial,
  `tcp://` and `unix:` transports, the `--boot-wait` flag and the regression
  test that exists (`TestEveryPeripheralVerbIsDispatched`); user.md no longer
  says four boards and links an anchor cli.md has.
