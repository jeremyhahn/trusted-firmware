### Fixed

- **docs/ethernet-transport: user.md and cli.md match tfwctl** (#640).
  user.md says a `netcfg set` value tfwctl cannot parse is refused naming the
  flag, and a missing one or one the encoder cannot carry by the SDK's
  `*tfw.NetConfigError` naming the field; cli.md's exit-2 row excepts
  `tfwctl ports`, which lists the serial ports and exits 0.
