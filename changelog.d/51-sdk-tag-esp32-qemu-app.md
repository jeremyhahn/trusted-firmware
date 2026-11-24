### Changed

- **The application SDK builds against its release** (#51). `make
  app-sdk-check` ran at `v0.4.2-alpha`: the example application built out of
  tree against the released headers and ran on tfwsim, so the row's "builds
  against a released SDK version" line is done.
- **`esp32-qemu-test` boots the example application** (#51). Its esp32c3
  build links the example into an image of its own, and `qemu_test.py` boots
  it once: `wirecheck -app-name heartbeat` asks APP_INFO and requires the
  application named and running. The images without an application are never
  asked APP_INFO.
