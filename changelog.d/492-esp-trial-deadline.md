- test(esp32): host unit tests for the 60 s OTA trial deadline: a confirm
  before the deadline keeps the image, and no confirm by 60 s resets into
  ESP-IDF's rollback (#492).
