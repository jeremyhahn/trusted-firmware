### Fixed

- **`PulseIn` and `PulseCapture` refuse every negative timeout** (#75). A
  negative timeout under one microsecond, such as −500 ns, truncated to 0 µs
  and went to the device as a timeout it accepts; it now returns
  `*ArgRangeError` locally like −1 µs does, as
  `docs/host-sdk/engineering.md` requires of a truncating conversion. No wire
  change.
