- tfwctl's OTA rows stage an ESP-IDF app image on an ESP32 board (#562):
  ESP-IDF's stager refused the TFIM-only image's first chunk, which the
  ESP32-P4 answered as `crypto provider failed`. The P4's soft-tier image
  now refuses the row's image at its app signature check, as it should; the
  row needs an image signed with the build's key to pass.
