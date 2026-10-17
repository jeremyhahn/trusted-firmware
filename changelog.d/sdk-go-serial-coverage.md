- sdk/go: `serialport.Open` is tested against a pseudo-terminal pair (8N1 and
  the baud default, bytes both ways in raw mode, the read-timeout floor, the
  receive purge, a missing path and a path that is not a tty), and
  `PulseCapture` against its documented contract on a canned link. Coverage:
  `sdk/go/serialport` 0.0% → 68.8%; `PulseCapture` 28.6% → 100%.
