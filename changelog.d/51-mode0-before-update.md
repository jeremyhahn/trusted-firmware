### Fixed

- **`before_update` before the reset that installs an `OTA_ACTIVATE` mode 0
  image** (#51). Mode 0 marks the staged image and resets nothing; the
  `RESET` mode 0 that follows installs it, and the application heard only
  `shutdown`. It now hears `before_update` then `shutdown`, as on `RESET`
  mode 1 and `OTA_ACTIVATE` mode 1. The firmware asks the stager, from the
  session's state and the MARK's magic in the store, with no new RAM: a later
  `OTA_BEGIN`, a failed stage or a failed second activation clears it, and a
  `RESET` with nothing marked still calls only `shutdown`. The
  `application-sdk` row's `before_update` is shipped; `after_update` remains
  the `ota` row's.
