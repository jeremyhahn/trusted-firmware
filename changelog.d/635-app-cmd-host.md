### Added

- **The host side of APP_CMD (0x07): `tfw.CmdAppCmd`, `Device.AppCommand(ctx, op, args)` and `tfwctl app-cmd <op> [hex-args]`**, as the application wire-commands ADR decides. The reply comes back undecoded; more than 63 bytes of args is refused before anything is sent, and the device's UNKNOWN_CMD, BAD_STATE, UNSUPPORTED and AUTH_REQUIRED answers come back as its status errors (#635).
