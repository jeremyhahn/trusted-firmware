### Fixed

- **tfwctl refuses a `tcp://` address whose port is not 1-65535** (#55): an
  empty, zero, named or out-of-range port (`tcp://:`, `tcp://h:`, `tcp://h:0`,
  `tcp://h:http`, `tcp://h:65536`) is an error at once, not a full `-timeout`
  wait on a random port no device's network block can name.
