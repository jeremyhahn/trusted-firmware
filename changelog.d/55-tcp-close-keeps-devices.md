### Added

- **Tests for two promises in sdk/go/tcp.go** (#55).
  `TestAnAcceptedDeviceOutlivesTheListenersClose` holds tcp.go:61, "Devices
  already accepted stay connected": a device accepted before the Listener's
  Close still answers a ping after it. `TestEveryAcceptedDeviceGetsTheListenersOptions`
  holds tcp.go:28, "The options apply to every accepted Device": the
  listener's WithTimeout ends a silent device's Do on the second accepted
  device as well as the first.
