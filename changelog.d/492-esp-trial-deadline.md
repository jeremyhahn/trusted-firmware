- test(esp32): host unit tests for the 60 s OTA trial deadline: a confirm
  before the deadline keeps the image, and no confirm by 60 s resets into
  ESP-IDF's rollback (#492). The suite builds against the esp32-qemu board
  profile, so it tests the deadline an ESP32 image has.
