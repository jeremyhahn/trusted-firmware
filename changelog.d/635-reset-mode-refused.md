### Fixed

- **`tfwctl reset` refuses a mode other than `soft` or `boot`** (#635). The
  verb takes `reset [soft|boot]` (`docs/wire-protocol/cli.md`), but any other
  word, such as `tfwctl reset bot`, was taken as a soft reset and `RESET(0)`
  sent, restarting the device and its application; a second word was dropped.
  It now returns `usage: reset [soft|boot]` before the wire, as `mode` does.
