### Fixed

- On the ESP32, a tone start whose LEDC channel will not configure puts the shared tone timer back to the running tone's frequency, so a refused start no longer changes the pitch of the tone already sounding (Refs #48).
