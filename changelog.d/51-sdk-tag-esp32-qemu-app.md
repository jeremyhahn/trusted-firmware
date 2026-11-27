### Changed

- **`esp32-qemu-test` boots the example application** (#51). Its esp32c3
  build links the example into an image of its own, and `qemu_test.py` boots
  it once: `wirecheck -app-name heartbeat` asks APP_INFO and requires the
  application named and running. The images without an application are never
  asked APP_INFO.
