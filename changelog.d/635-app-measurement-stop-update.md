### Added

- **`test_app_measurement` pins claim 273 after a stop for an update** (#635):
  once the runner stops an application with its before_update hook, the
  measurement is the firmware hash, and while it runs the application is asked
  for its 32 bytes on every call, not once.
